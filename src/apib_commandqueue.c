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

#include <pthread.h>

#include "src/apib_iothread.h"

void command_Init(CommandQueue* q) {
  q->head = NULL;
  pthread_mutex_init(&(q->lock), NULL);
}

void command_Free(CommandQueue* q) {
  pthread_mutex_destroy(&(q->lock));
}

void command_Add(CommandQueue* q, Command* cmd) {
  cmd->next = NULL;
  pthread_mutex_lock(&(q->lock));
  if (q->head == NULL) {
    q->head = cmd;
  } else {
    Command* last = q->head;
    while (last->next != NULL) {
      last = last->next;
    }
    last->next = cmd;
  }
  pthread_mutex_unlock(&(q->lock));
}

Command* command_Pop(CommandQueue* q) {
  pthread_mutex_lock(&(q->lock));
  Command* cmd = q->head;
  if (cmd != NULL) {
    q->head = cmd->next;
  }
  pthread_mutex_unlock(&(q->lock));
  return cmd;
}