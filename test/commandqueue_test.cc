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

#include "src/apib_iothread.h"

#include "gtest/gtest.h"

TEST(CommandQueue, Basic) {
  CommandQueue queue;
  command_Init(&queue);
  Command* c1 = (Command*)malloc(sizeof(Command));
  c1->newNumConnections = 1;
  command_Add(&queue, c1);
  Command* head = command_Pop(&queue);
  EXPECT_EQ(c1, head);
  free(c1);
  head = command_Pop(&queue);
  EXPECT_EQ(NULL, head);
  head = command_Pop(&queue);
  EXPECT_EQ(NULL, head);
  command_Free(&queue);
}

TEST(CommandQueue, Larger) {
  CommandQueue queue;
  command_Init(&queue);
  Command* c1 = (Command*)malloc(sizeof(Command));
  c1->newNumConnections = 1;
  command_Add(&queue, c1);
  Command* c2 = (Command*)malloc(sizeof(Command));
  c2->newNumConnections = 2;
  command_Add(&queue, c2);
  Command* c3 = (Command*)malloc(sizeof(Command));
  c3->newNumConnections = 3;
  command_Add(&queue, c3);

  Command* head = command_Pop(&queue);
  EXPECT_EQ(c1, head);
  EXPECT_EQ(1, head->newNumConnections);
  free(head);

  head = command_Pop(&queue);
  EXPECT_EQ(c2, head);
  EXPECT_EQ(2, head->newNumConnections);
  free(head);

  head = command_Pop(&queue);
  EXPECT_EQ(c3, head);
  EXPECT_EQ(3, head->newNumConnections);
  free(head);

  head = command_Pop(&queue);
  EXPECT_EQ(NULL, head);
}