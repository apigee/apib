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

#ifndef APIB_RAND_H
#define APIB_RAND_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* RandState;

// Create a random state. These are not thread-safe.
extern RandState apib_InitRand();
extern void apib_FreeRand(RandState s);

// Get a random integer
extern long apib_Rand(RandState s);

#ifdef __cplusplus
}
#endif

#endif  //  APIB_RAND_H
