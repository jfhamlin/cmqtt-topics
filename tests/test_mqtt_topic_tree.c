#include <stdio.h>
#include <string.h>

#include "CuTest.h"

#include "mqtt_topic_tree.h"

#define ARRAY_EL_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

int initialized = 0;
char *topics[] = {
  "",            // 0 (Not a valid topic, but search supports it.)
  "/",           // 1
  "a",           // 2
  "a/b",         // 3
  "a/c",         // 4
  "b",           // 5
  "b/c",         // 6
  "b/d",         // 7
  "b/c/zoo",     // 8
  "//",          // 9
  "///",         // 10
  "+/c",         // 11
  "b/#",         // 12
  "+/b",         // 13
  "+",           // 14
  "foo",         // 15
  "foo/#",       // 16
  "foo/+",       // 17
  "foo/+/baz",   // 18
  "foo/+/baz/#", // 19
  "$SYS/test",   // 20
  "$BAD/test",   // 21
  "b/$SYS",      // 22
};

typedef struct {
  /* Pattern to test against topic hierarchy. */
  char *pattern;

  /* Array of indices of matching topics, terminated by -1. */
  int matches[ARRAY_EL_COUNT(topics) + 1];
} pattern_match_s;

pattern_match_s pattern_matches[] = {
  { "", { 0, 14, -1 } },
  { "+", { 0, 2, 5, 12, 14, 15, 16, -1 } },
  { "#", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 22, -1 } },
  { "/z", { -1 } },
  { "b/c", { 6, 11, 12, -1 } },
  { "+/c", { 4, 6, 11, 12, 16, 17, -1 } },
  { "b/+/zoo", { 8, -1 } },
  { "b/+", { 6, 7, 11, 12, 13, 22, -1 } },
  { "b/#", { 5, 6, 7, 8, 11, 12, 13, 14, 22, -1 } },
  { "foo/bar/baz", { 16, 18, 19, -1 } },
};

/**
 * C doesn't allow modification of string literals. Since the topic
 * tree code temporarily modifies input topics, we have to duplicate
 * the literals before passing them in. We don't bother to free these
 * strings (or anything in the tests, really).
 */
static void init() {
  if (initialized) return;
  initialized = 1;
  for (int i = 0; i < ARRAY_EL_COUNT(topics); ++i) {
    topics[i] = strdup(topics[i]);
  }

  for (int i = 0; i < ARRAY_EL_COUNT(pattern_matches); ++i) {
    pattern_matches[i].pattern = strdup(pattern_matches[i].pattern);
  }
}

/**
 * expected_count returns the expected number of matches in a pattern_match_s.
 */
int expected_count(pattern_match_s *m) {
  int i;
  for (i = 0; m->matches[i] != -1; ++i) { }
  return i;
}

/**
 * topic_depth returns the depth of a topic, where a topic with one
 * level has depth 1.
 */
static int topic_depth(char *t) {
  int depth = 0;
  while ((t = strchr(t, '/'))) {
    ++depth;
    ++t;
  }
  return depth + 1;
}

/**
 * segment_depth returns the depth of a topic segment, where the first
 * segment in a topic has depth 1.
 */
static int segment_depth(mqtt_topic_segment_s *s) {
  int depth = 0;
  while ((s = s->parent)) ++depth;
  return depth;
}

/* Buffer for test failure messages. */
char msg[1024];

/**
 * Test topic insertion.
 */
void Test_topic_find_or_add(CuTest *tc) {
  mqtt_topic_segment_s *seg = NULL;
  mqtt_topic_segment_s *root = NULL;
  init();

  root = mqtt_topic_segment_create();
  CuAssertPtrNotNull(tc, root);
  for (int i = 0; i < ARRAY_EL_COUNT(topics); ++i) {
    int rc = mqtt_topic_find_or_add(&seg, root, topics[i], 1);
    CuAssertTrue(tc, !rc);
    sprintf(msg, "'%s': depth check, %d", topics[i], topic_depth(topics[i]));
    CuAssertIntEquals_Msg(tc, msg, topic_depth(topics[i]), segment_depth(seg));
  }
}

/**
 * Test topic lookup.
 */
void Test_topic_find(CuTest *tc) {
  mqtt_topic_segment_s *seg = NULL;
  mqtt_topic_segment_s *root = NULL;
  init();

  char *created[] = {
    "/",
    "a/c",
    "#",
    "foo/+/bar/+/baz",
  };

  root = mqtt_topic_segment_create();
  for (int i = 0; i < ARRAY_EL_COUNT(created); ++i) {
    created[i] = strdup(created[i]);
    mqtt_topic_find_or_add(&seg, root, created[i], 1);
  }

  for (int i = 0; i < ARRAY_EL_COUNT(created); ++i) {
    int rc = mqtt_topic_find_or_add(&seg, root, strdup(created[i]), 0);
    sprintf(msg, "'%s' should exist", created[i]);
    CuAssertIntEquals_Msg(tc, msg, 0, rc);
    sprintf(msg, "'%s': depth check, %d", created[i], topic_depth(created[i]));
    CuAssertIntEquals_Msg(tc, msg, topic_depth(created[i]), segment_depth(seg));
  }

  char *not_created[] = {
    "//",
    "a/c/d",
    "a/#",
    "foo/bar/+/baz",
  };

  for (int i = 0; i < ARRAY_EL_COUNT(not_created); ++i) {
    int rc = mqtt_topic_find_or_add(&seg, root, strdup(not_created[i]), 0);
    sprintf(msg, "'%s' should not be found", not_created[i]);
    CuAssertIntEquals_Msg(tc, msg, 1, rc);
    CuAssertPtrEquals(tc, NULL, seg);
  }
}

/**
 * Test topic matching.
 */

typedef struct {
  int count;
  pattern_match_s match;
  CuTest *tc;
} cb_data_s;

void matcher(void *data, char *topic, mqtt_topic_segment_s *segment) {
  cb_data_s *d = data;
  ++d->count;

  for (int i = 0; d->match.matches[i] != -1; ++i) {
    if (strcmp(topics[d->match.matches[i]], topic) == 0) {
      return;
    }
  }
  sprintf(msg, "'%s' unexpected match: '%s'", d->match.pattern, topic);
  CuAssertPtrNotNullMsg(d->tc, msg, NULL);
}

void Test_mqtt_topic_matching_iter(CuTest *tc) {
  mqtt_topic_segment_s *seg = NULL;
  mqtt_topic_segment_s *root = NULL;
  init();

  root = mqtt_topic_segment_create();
  CuAssertPtrNotNull(tc, root);
  for (int i = 0; i < ARRAY_EL_COUNT(topics); ++i) {
    int rc = mqtt_topic_find_or_add(&seg, root, topics[i], 1);
    CuAssertTrue(tc, !rc);
  }

  for (int i = 0; i < ARRAY_EL_COUNT(pattern_matches); ++i) {
    cb_data_s data = {
      .count = 0,
      .match = pattern_matches[i],
      .tc = tc,
    };
    mqtt_iter_cb_s cb = {
      .data = &data,
      .fn = &matcher,
    };
    mqtt_topic_matching_iter(root, pattern_matches[i].pattern, &cb);
    sprintf(msg, "'%s': pat check", pattern_matches[i].pattern);
    CuAssertIntEquals_Msg(tc, msg,
                          expected_count(&pattern_matches[i]), data.count);
  }
}

void counter(void *data, char *topic, mqtt_topic_segment_s *segment) {
  ++(*(int *)data);
}

void Test_mqtt_topic_iter(CuTest *tc) {
  mqtt_topic_segment_s *seg = NULL;
  mqtt_topic_segment_s *root = NULL;
  init();

  root = mqtt_topic_segment_create();
  CuAssertPtrNotNull(tc, root);
  for (int i = 0; i < ARRAY_EL_COUNT(topics); ++i) {
    int rc = mqtt_topic_find_or_add(&seg, root, topics[i], 1);
    CuAssertTrue(tc, !rc);
  }

  int count = 0;
  mqtt_iter_cb_s cb = {
    .data = &count,
    .fn = &counter,
  };
  mqtt_topic_iter(root, &cb);
  CuAssertIntEquals(tc, 25, count);
}

void Test_mqtt_topic_validate(CuTest *tc) {
  char *valid[] = {
    "/",
    "aa/bb",
    "+",
    "+/xyz",
    "xyz/+",
    "xyz/+/abc",
    "#",
    "abc/#",
    "test////a//topic",
  };
  char *invalid[] = {
    "",
    "#/abc",
    "a+",
    "f#",
    "/#a",
    "/+a",
  };

  for (int i = 0; i < ARRAY_EL_COUNT(valid); ++i) {
    sprintf(msg, "'%s': expected to be valid", valid[i]);
    CuAssertTrueMsg(tc, msg, mqtt_topic_validate(valid[i]));
  }

  for (int i = 0; i < ARRAY_EL_COUNT(invalid); ++i) {
    sprintf(msg, "'%s': expected to be invalid", invalid[i]);
    CuAssertFalseMsg(tc, msg, mqtt_topic_validate(invalid[i]));
  }
}

/**
 * TODO: Test case where we can't release, etc.
 */
void Test_mqtt_topic_remove(CuTest *tc){
  mqtt_topic_segment_s *seg = NULL;
  mqtt_topic_segment_s *root = NULL;
  init();

  root = mqtt_topic_segment_create();
  CuAssertPtrNotNull(tc, root);
  for (int i = 0; i < ARRAY_EL_COUNT(topics); ++i) {
    mqtt_topic_find_or_add(&seg, root, topics[i], 1);
  }

  CuAssertFalse(tc, mqtt_topic_find_or_add(&seg, root, topics[8], 0));
  CuAssertFalse(tc, mqtt_topic_segment_remove(seg));
  CuAssertIntEquals_Msg(tc, "The topic should have been removed.",
                        1, mqtt_topic_find_or_add(&seg, root, topics[8], 0));
  CuAssertIntEquals_Msg(tc, "A sibling topic should not have been removed.",
                        0, mqtt_topic_find_or_add(&seg, root, topics[7], 0));
}
