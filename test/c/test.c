//
// Created by hariharan on 8/8/22.
//
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dftracer/dftracer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int bar();
void foo() {
  DFTRACER_C_FUNCTION_START();
  DFTRACER_C_FUNCTION_UPDATE_INT("key", 0);
  DFTRACER_C_FUNCTION_UPDATE_STR("key", "0");
  usleep(1000);
  DFTRACER_C_REGION_START(CUSTOM);
  DFTRACER_C_REGION_UPDATE_INT(CUSTOM, "key", 0);
  DFTRACER_C_REGION_UPDATE_STR(CUSTOM, "key", "0");
  bar();
  usleep(1000);
  DFTRACER_C_REGION_START(CUSTOM_BLOCK);
  usleep(1000);
  DFTRACER_C_REGION_END(CUSTOM_BLOCK);
  DFTRACER_C_REGION_END(CUSTOM);
  DFTRACER_C_FUNCTION_END();
}

int main(int argc, char* argv[]) {
  if (argc > 2) {
    if (strcmp(argv[2], "1") == 0) {
      DFTRACER_C_INIT(NULL, NULL, NULL);
    }
  }
  DFTRACER_C_METADATA(meta, "key", "value");
  char filename[1024];
  sprintf(filename, "%s/demofile_c.txt", argv[1]);
  foo();
  FILE* fh = fopen(filename, "w+");
  fwrite("hello", sizeof("hello"), 1, fh);
  int child_pid = fork();  // fork a duplicate process
  int pid = getpid();
  int child_ppid = getppid();  // get the child's parent pid
  printf("child_pid:%d ppid:%d pid:%d\n", child_pid, child_ppid, pid);

  if (child_pid == 0) {
    // Child process: execute a lightweight command and exit.
    char* arr[] = {"ls", "-l", NULL};
    execv("/bin/ls", arr);
    DFTRACER_C_FINI();
    _exit(1);
  }
  int status = -1;
  waitpid(child_pid, &status, WEXITED);
  fclose(fh);
  DFTRACER_C_FINI();
  return 0;
}

// Add definition for bar() to fix the error
int bar() {
  // Dummy implementation
  return 0;
}
