#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

int is_blank(char c) {
  return strchr(" \n\r\t\v", c) != NULL;
}

int is_delim(char c) {
  return c == '<' || c == '>' || c == '|';
}

int get_tokens(const char *cmdline, char** argv) {
  int argc = 0;
  const char* pc = cmdline;
  char buf[100];
  char* pb;

  while(pc != 0) {
    pb = buf;
    while(*pc != 0 && is_blank(*pc)) {
      pc++;
    }

    if (*pc == 0)break;

    while(*pc != 0 && !is_blank(*pc)) {
      if (is_delim(*pc)) {
        if ( pb == buf ) {
          *pb = *pc;
          pb++; pc++;
        }
        break;
      }
      *pb = *pc;
      pb++; pc++;
    }
    *pb = 0;
    int len = pb - buf;
    argv[argc] = (char*)malloc(len + 1);
    strncpy(argv[argc], buf, len);
    argc++;
  }
  argv[argc] = NULL;
  return 0;
}


void token_dump(char** argv){
  int c = 0;
  while(argv[c] != NULL) {
    printf("%s\n", argv[c]);
    c++;
  }
}
