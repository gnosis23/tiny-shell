#include <stdio.h>
#include "parser.h"

int main(int argc, const char *argv[])
{
  char buf[100];
  char *v[100];
  struct cmd* cmd;
  while(fgets(buf, sizeof(buf), stdin) != NULL) {
    get_tokens(buf, v); 
    token_dump(v);
    printf("background %d\n", is_background(v));
    cmd = parsecmd(v);

    printf("===== dump =====\n");
    cmd_dump(cmd);
    printf("\n======\n");
    token_clear(v);
  }
  return 0;
}
