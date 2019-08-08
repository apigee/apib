/*
Copyright 2019 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <limits.h>
#include <stdlib.h>

#include "src/apib_priorityq.h"

#include "gtest/gtest.h"

class PQTest : public ::testing::Test {
 protected:
  pq_Queue* q;

  PQTest() {
    q = pq_Create();
  }

  ~PQTest() {
    pq_Free(q);
  }
};

TEST_F(PQTest, Empty) {
  ASSERT_EQ(nullptr, pq_Peek(q));
  ASSERT_EQ(nullptr, pq_Pop(q));
}

TEST_F(PQTest, Priorities) {
  int one = 1;
  int two = 2;
  int three = 3;

  pq_Push(q, &one, 1);
  pq_Push(q, &two, 100);
  pq_Push(q, &three, 10);

  ASSERT_EQ(&one, pq_Peek(q));
  ASSERT_EQ(1, pq_PeekPriority(q));
  ASSERT_EQ(&one, pq_Pop(q));
  ASSERT_EQ(&three, pq_Pop(q));
  ASSERT_EQ(&two, pq_Pop(q));
  ASSERT_EQ(nullptr, pq_Pop(q));
}

typedef struct {
  int index;
  int priority;
} PQM;

static PQM* makeItems(int s) {
  PQM* p = (PQM*)malloc(sizeof(PQM) * s);
  for (int i = 0; i < s; i++) {
    p[i].index = i;
    // Generate ordinary random numbers here, with no seed.
    // This way the test will be reproducable.
    p[i].priority = rand();
  }
  return p;
}

static int comparePQM(const void* i1, const void* i2) {
  const PQM* p1 = (const PQM*)i1;
  const PQM* p2 = (const PQM*)i2;

  if (p1->priority < p2->priority) {
    return -1;
  } else if (p1->priority > p2->priority) {
    return 1;
  }
  return 0;
}

#define ITEMS 1000

// Test that we can accurately make a list of random items,
// sort them, and get a sorted order. This is a test of the test
// code...
TEST_F(PQTest, ItemSort) {
  PQM* p = makeItems(ITEMS);
  qsort(p, ITEMS, sizeof(PQM), comparePQM);

  int last = INT_MIN;
  for (int i = 0; i < ITEMS; i++) {
    ASSERT_LT(last, p[i].priority);
    last = p[i].priority;
  }
}

// Make the random list of items, put them in a queue, and ensure that
// they come off in priority order.
TEST_F(PQTest, PrioritySort) {
  // Enqueue the random list of itesms
  PQM* p = makeItems(ITEMS);
  for (int i = 0; i < ITEMS; i++) {
    pq_Push(q, &(p[i]), p[i].priority);
  }

  // Make a copy of the list and sort it
  PQM* sortedP = (PQM*)malloc(sizeof(PQM) * ITEMS);
  memcpy(sortedP, p, sizeof(PQM) * ITEMS);
  qsort(sortedP, ITEMS, sizeof(PQM), comparePQM);

  // Make sure items came off in the sorted order
  for (int i = 0; i < ITEMS; i++) {
    ASSERT_EQ(sortedP[i].priority, pq_PeekPriority(q));
    PQM* top = (PQM*)pq_Pop(q);
    ASSERT_EQ(sortedP[i].index, top->index);
  }
}
