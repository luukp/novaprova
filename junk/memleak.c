/*
 * Copyright 2014-2020 Gregory Banks
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#if HAVE_MALLOC_H
#include <malloc.h>
#endif

void a_function(void)
{
    malloc(32);
}

int main(int argc, char **argv)
{
    printf("Hello world\n");
    a_function();
    return 0;
}


