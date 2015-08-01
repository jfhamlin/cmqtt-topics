#ifndef _MQTT_TOPIC_TREE_H_
#define _MQTT_TOPIC_TREE_H_

#include "red_black_tree.h"

/**
 * Required operations:
 * - Add or remove a topic.
 * - Find a topic literal.
 * - Find all topics matching a pattern.
 */

/**
 * mqtt_topic_validate returns 1 if topic is a valid topic string, 0
 * otherwise.
 */
int mqtt_topic_validate(const char *topic);

/**
 *
 */
typedef struct mqtt_topic_segment {
  /* The string for this topic segment. */
  const char *str;

  /* The parent segment, or the sentinel segment if this is a
   * top-level segment. */
  struct mqtt_topic_segment *parent;

  /* A tree of child topic segments. */
  rb_red_blk_tree *children;

  /* A child # segment. Not kept in the children tree, for simpler access. */
  //struct mqtt_topic_segment *hash_child;
  /* A child + segment. Not kept in the children tree, for simpler access. */
  //struct mqtt_topic_segment *plus_child;

  /* The data associated with the topic terminating with this segment,
   * if any. Management of data memory is the responsibility of the
   * client. */
  void *data;
} mqtt_topic_segment_s;

/**
 * mqtt_topic_segment_create creates a new mqtt_topic_segment_s. This
 * function should only be used to create the root, sentinel segment.
 */
mqtt_topic_segment_s *mqtt_topic_segment_create();

/**
 * mqtt_topic_segment_create destroys a mqtt_topic_segment_s. This
 * function should only be used to destroy the root, sentinel segment
 * after all user data in the tree has been freed. Otherwise, the
 * memory may be leaked.
 */
void mqtt_topic_segment_destroy(mqtt_topic_segment_s *s);

/**
 * mqtt_topic_find_or_add returns the final segment in a topic (in
 * *h_segment).  If create != 0, missing topic segments will be
 * created if they don't yet exist. If create == 0, *h_segment will be
 * NULL if the final topic segment could not be found. In this and all
 * other topic tree functions, root is a sentinel segment that does
 * not form part of the topic.
 *
 * Returns:
 *  0 if the topic was found or added.
 *  -1 if out of memory.
 *  1 if the topic could not be found.
 */
int mqtt_topic_find_or_add(mqtt_topic_segment_s **h_segment,
                           mqtt_topic_segment_s *root,
                           char *topic, int create);

/**
 * mqtt_topic_segment_remove removes a segment from the topic tree if
 * data is NULL and it has no children, recursively removing childless
 * ancestors whose data field is also NULL. The sentinel segment
 * cannot be destroyed with this function. Use
 * mqtt_topic_segment_destroy to destroy the entire tree.
 *
 * Returns 0 on success, -1 if out of memory.
 */
int mqtt_topic_segment_remove(mqtt_topic_segment_s *segment);

/**
 * mqtt_iter_cb_s holds a callback (fn) called for each matching topic
 * encountered in a call to mqtt_topic_matching_iter.
 */
typedef struct {
  void *data;
  void (*fn)(void* data, char *topic, mqtt_topic_segment_s *segment);
} mqtt_iter_cb_s;

/**
 * mqtt_topic_matching_iter calls cb for every segment that terminates
 * a topic that matches pattern. A pattern is a topic that may contain
 * wildcards (+ or #). A literal topic is a topic that may not contain
 * wildcards. Normal uses of topic matching include matching publishes
 * against subscriptions (literal topics against topic patterns) and
 * subscriptions against topics with retained messages (topic patterns
 * against literal topics). For generality, topic pattern matching
 * performed by this function is extended beyond the MQTT
 * specification to permit matching patterns against other
 * patterns. Two patterns match each other if the intersection of the
 * sets of topics they describe is non-empty.
 *
 * Examples:
 * - # matches all topics
 * - a/# matches a/b, a/b/c, +/b/c, etc.
 * - b/c/d matches b/c/d, b/+/d, b/#, etc.
 *
 * It is illegal to call mqtt_topic_remove_segment on a segment from
 * cb.
 */
void mqtt_topic_matching_iter(mqtt_topic_segment_s *root,
                              char *pattern, mqtt_iter_cb_s *cb);

/**
 * mqtt_topic_iter visits every segment in a topic tree. It is illegal
 * to call mqtt_topic_remove_segment on a segment from cb.
 */
void mqtt_topic_iter(mqtt_topic_segment_s *root, mqtt_iter_cb_s *cb);

#endif
