#include <stdio.h>
#include "parser.h"

int main(int argc, const char *argv[])
{
  char buf[100];
  char *v[100];
  while(fgets(buf, sizeof(buf), stdin) != NULL) {
    get_tokens(buf, v); 
    token_dump(v);
  }
  return 0;
}
