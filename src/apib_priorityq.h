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

#ifndef APIB_PRIORITYQ_H
#define APIB_PRIORITYQ_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct 
{
  long long weight;
  void* item;
} pq_Item;

typedef struct 
{
  int size;
  int allocated;
  pq_Item* items;
} pq_Queue;

extern pq_Queue* pq_Create();
extern void pq_Free(pq_Queue* q);
extern void pq_Push(pq_Queue* q, void* item, long long priority);
extern void* pq_Pop(pq_Queue* q);
extern const void* pq_Peek(const pq_Queue* q);
extern long long pq_PeekPriority(const pq_Queue* q);

#ifdef __cplusplus
}
#endif

#endif  // APIB_PRIORITYQ_H
