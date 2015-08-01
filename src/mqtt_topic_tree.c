#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mqtt_topic_tree.h"

/* This length includes the terminator. */
#define MAX_TOPIC_LENGTH 65536

static char scratch_topic[MAX_TOPIC_LENGTH] = { '\0' };
static int scratch_topic_length = 0;

/**
 *
 */
static void scratch_topic_push(const char *seg, int first) {
    sprintf(scratch_topic + scratch_topic_length, "%s%s",
            (first ? "" : "/"), seg);
    scratch_topic_length += strlen(seg) + 1 - first;
}

/**
 *
 */
static void scratch_topic_pop() {
  while(scratch_topic_length > 0 &&
        scratch_topic[scratch_topic_length] != '/') {
    --scratch_topic_length;
  }
  scratch_topic[scratch_topic_length] = '\0';
}

static int rb_cmp(const void *a, const void *b) {
  int cmp = strcmp((char *)a, (char *)b);
  if (cmp < 0) {
    return -1;
  } else if (cmp > 0) {
    return 1;
  } else {
    return 0;
  }
}

static void rb_destroy_key(void *a) {
  free(a);
}

static void rb_destroy_info(void *a) {
  mqtt_topic_segment_destroy((mqtt_topic_segment_s *)a);
}

static rb_red_blk_tree *create_rb_tree() {
  return RBTreeCreate(&rb_cmp, &rb_destroy_key, &rb_destroy_info, NULL, NULL);
}

mqtt_topic_segment_s *mqtt_topic_segment_create() {
  mqtt_topic_segment_s *s;

  s = malloc(sizeof(mqtt_topic_segment_s));
  if (s == NULL) {
    return NULL;
  }

  memset(s, 0, sizeof(*s));
  s->children = create_rb_tree();
  if (s->children == NULL) {
    free(s);
    return NULL;
  }

  return s;
}

void mqtt_topic_segment_destroy(mqtt_topic_segment_s *s) {
  if (s == NULL) return;

  RBTreeDestroy(s->children);
  free(s);
}

int mqtt_topic_segment_remove(mqtt_topic_segment_s *s) {
  mqtt_topic_segment_s *parent = s->parent;
  /* The sentinel segment cannot be removed, as it isn't a part of the
   * topic tree. It can only destroyed. */
  if (parent == NULL) {
    return 0;
  }

  /* Do not remove segments with user data or with remaining
   * children. */
  if (s->data || s->children->root->left != s->children->nil) {
    return 0;
  }

  rb_red_blk_tree *tree = parent->children;
  /* node must be found. */
  rb_red_blk_node *node = RBExactQuery(tree, (void *)s->str);
  RBDelete(tree, node);

  return mqtt_topic_segment_remove(parent);
}

/**
 * mqtt_topic_validate returns 1 if topic is a valid MQTT topic, 0
 * otherwise.
 *
 * Assuming a topic of at least one character, the following finite
 * state machine describes valid topics, where all states are accepting:
 *  Start:
 *    # -> Hash
 *    + -> Plus
 *    / -> Start
 *    [^+#/] -> Literal
 *  Hash:
 *    NO TRANSITIONS
 *  Plus:
 *    / -> Start
 *  Literal:
 *    [^+#/] -> Literal
 *    / -> Start
 */
int mqtt_topic_validate(const char *topic) {
  enum {
    START,
    HASH,
    PLUS,
    LITERAL,
  } state = START;

  if (topic[0] == '\0') {
    return 0;
  }

  for (; topic[0]; ++topic) {
    char c = topic[0];
    switch (state) {
      case START:
      {
        switch (c) {
          case '#':
            state = HASH;
            break;
          case '+':
            state = PLUS;
            break;
          case '/':
            break;
          default:
            state = LITERAL;
            break;
        }
        break;
      }
      case HASH:
      {
        return 0; /* '#' must be the final character. */
      }
      case PLUS:
      {
        if (c != '/') {
          return 0;
        }
        state = START;
        break;
      }
      case LITERAL:
      {
        if (c == '+' || c == '#') {
          return 0;
        } else if (c == '/') {
          state = START;
        }
        break;
      }
    }
  }
  return 1;
}

int mqtt_topic_find_or_add(mqtt_topic_segment_s **h_segment,
                           mqtt_topic_segment_s *root,
                           char *topic, int create) {
  int rc;
  char *next_segment = topic, *rest, *sep;
  mqtt_topic_segment_s *new_segment;
  rb_red_blk_node *child;

  if (topic == NULL) {
    *h_segment = root;
    return 0;
  }

  *h_segment = NULL;

  sep = strchr(topic, '/');
  if (sep == NULL) {
    rest = NULL;
  } else {
    sep[0] = '\0';
    rest = sep + 1;
  }

  child = RBExactQuery(root->children, next_segment);
  if (child != NULL) {
    rc = mqtt_topic_find_or_add(h_segment,
                                (mqtt_topic_segment_s *)child->info,
                                rest, create);
    goto exit;
  }

  if (!create) {
    rc = 1;
    goto exit;
  }

  new_segment = mqtt_topic_segment_create();
  if (new_segment == NULL) {
    rc = -1;
    goto exit;
  }

  new_segment->parent = root;

  char *key = strdup(next_segment);
  if (key == NULL){
    rc = -1;
    mqtt_topic_segment_destroy(new_segment);
    goto exit;
  }

  new_segment->str = key;

  child = RBTreeInsert(root->children, key, new_segment);
  if (child == NULL) {
    rc = -1;
    free(key);
    mqtt_topic_segment_destroy(new_segment);
    goto exit;
  }

  rc = mqtt_topic_find_or_add(h_segment, new_segment, rest, create);
  if (rc != 0) {
    free(key);
    mqtt_topic_segment_destroy(new_segment);
    goto exit;
  }

exit:

  if (sep) {
    *sep = '/';
  }

  return rc;
}

/**
 *
 */
static void _red_black_cb_all(rb_red_blk_tree *tree,
                              rb_red_blk_node *node,
                              int first, int ignore_sys,
                              mqtt_iter_cb_s *cb);

/**
 *
 */
static void _segment_cb_all(mqtt_topic_segment_s *segment,
                            mqtt_iter_cb_s *cb) {
  cb->fn(cb->data, scratch_topic, segment);
  _red_black_cb_all(segment->children, segment->children->root,
                    /* ignore_sys value is irrelevant here, since we
                     * must be beyond the first level. */
                    0, 0,
                    cb);
}

static void _red_black_cb_all(rb_red_blk_tree *tree,
                              rb_red_blk_node *node,
                              int first, int ignore_sys,
                              mqtt_iter_cb_s *cb) {
  int skip_node;

  if (node == tree->nil) {
    return;
  }

  /* Ignore $-prefixed keys only if this is the first level and the
   * ignore_sys flag is set. */
  skip_node = node == tree->root || (
      first && ignore_sys &&
      ((char *)node->key)[0] == '$'
    );

  _red_black_cb_all(tree, node->left, first, ignore_sys, cb);

  if (!skip_node) {
    scratch_topic_push((char *)node->key, first);
    _segment_cb_all((mqtt_topic_segment_s *)node->info, cb);
    scratch_topic_pop();
  }

  _red_black_cb_all(tree, node->right, first, ignore_sys, cb);
}

/**
 *
 */
static void _red_black_match_all(rb_red_blk_tree *tree,
                                 rb_red_blk_node *node,
                                 char *rest,
                                 int first,
                                 mqtt_iter_cb_s *cb) {
  if (node == tree->nil) {
    return;
  }

  _red_black_match_all(tree, node->left, rest, first, cb);

  if (node != tree->root && (!first || ((char *)node->key)[0] != '$')) {
    scratch_topic_push((char *)node->key, first);
    mqtt_topic_matching_iter((mqtt_topic_segment_s *)node->info, rest, cb);
    scratch_topic_pop();
  }

  _red_black_match_all(tree, node->right, rest, first, cb);
}

void mqtt_topic_matching_iter(mqtt_topic_segment_s *root,
                              char *pattern,
                              mqtt_iter_cb_s *cb) {
  char *next_segment = pattern, *rest, *sep;
  rb_red_blk_node *child;

  if (pattern == NULL) {
    cb->fn(cb->data, scratch_topic, root);
    child = RBExactQuery(root->children, "#");
    if (child) {
      /* A # matches its parent topic. */
      scratch_topic_push("#", (root->parent == NULL ? 1 : 0));
      cb->fn(cb->data, scratch_topic, child->info);
      scratch_topic_pop();
    }
    return;
  }

  sep = strchr(pattern, '/');
  if (sep == NULL) {
    rest = NULL;
  } else {
    *sep = '\0';
    rest = sep + 1;
  }

  if (strcmp(next_segment, "+") == 0) {
    /* Continue as though we matched all segments at the next level. */
    _red_black_match_all(root->children, root->children->root, rest,
                         (root->parent == NULL ? 1 : 0), cb);
    goto exit;
  } else if (strcmp(next_segment, "#") == 0) {
    if (root->parent /* i.e., this isn't the sentinel */) {
      /* A # matches its parent topic. */
      cb->fn(cb->data, scratch_topic, root);
    }

    /* Call the callback for all segments below this level. */
    _red_black_cb_all(root->children, root->children->root,
                      (root->parent == NULL ? 1 : 0), 1, cb);
    goto exit;
  } else {
    /* Check for wildcard topics, which also match pattern. */
    child = RBExactQuery(root->children, "+");
    if (child) {
      scratch_topic_push("+", (root->parent == NULL ? 1 : 0));
      mqtt_topic_matching_iter((mqtt_topic_segment_s *)child->info, rest, cb);
      scratch_topic_pop();
    }
    child = RBExactQuery(root->children, "#");
    if (child) {
      scratch_topic_push("#", (root->parent == NULL ? 1 : 0));
      cb->fn(cb->data, scratch_topic, child->info);
      scratch_topic_pop();
    }
  }

  child = RBExactQuery(root->children, next_segment);
  if (child) {
    scratch_topic_push(next_segment, (root->parent == NULL ? 1 : 0));
    mqtt_topic_matching_iter((mqtt_topic_segment_s *)child->info, rest, cb);
    scratch_topic_pop();
    goto exit;
  }

exit:
  if (sep) {
    *sep = '/';
  }
}

void mqtt_topic_iter(mqtt_topic_segment_s *root, mqtt_iter_cb_s *cb) {
  _red_black_cb_all(root->children, root->children->root,
                    1, 0, cb);
}
