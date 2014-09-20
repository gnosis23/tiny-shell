#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "parser.h"

int is_blank(char c) {
  return strchr(" \n\r\t\v", c) != NULL;
}

int is_delim(char c) {
  return c == '<' || c == '>' || c == '|' || c == '&';
}

int is_background(char** argv){
  int n = 0;
  while(argv[n] != NULL) n++;
  return n != 0 && strcmp(argv[n-1], "&") == 0;
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
    int len = pb - buf + 1;
    argv[argc] = (char*)malloc(len);
    /*
     * strncpy will copy trailing '\0'
     */
    strncpy(argv[argc], buf, len);
    argc++;
  }
  argv[argc] = NULL;
  return 0;
}

struct cmd* parsecmd(char** argv) {
  struct cmd *cmd;
  int p = 0;
  cmd = parseline(&p, argv);
  if (argv[p] != NULL) {
    fprintf(stderr, "leftover %s...\n", argv[p]);
    exit(-1);
  }
  return cmd;
}

struct cmd* parseline(int *no, char** argv) {
  struct cmd *cmd;
  cmd = parsepipe(no, argv);
  return cmd;
}

struct cmd* parsepipe(int *no, char** argv) {
  struct cmd *cmd;
  cmd = parseexec(no, argv);
  if (peek(no, argv, "|")) {
    *no += 1;
    cmd = make_pipecmd(cmd, parsepipe(no, argv));
  }  
  return cmd;
}

struct cmd* parseredirs(struct cmd *cmd, int *no, char** argv) {
  while(peek(no, argv, "<>")) {
    int tok = argv[*no][0];
    *no += 1;
    // 
    switch(tok) {
      case '<':
        cmd = make_redircmd(cmd, argv[*no], '<');
        break;
      case '>':
        cmd = make_redircmd(cmd, argv[*no], '>');
        break;
    }
    *no += 1;
  }
  return cmd;
}

struct cmd* parseexec(int *no, char** argv) {
  int argc;
  struct execcmd *cmd;
  struct cmd *ret;

  ret = make_cmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, no, argv);
  while(!peek(no, argv, "|")) {
    if (argv[*no]  == NULL) break;
    if (is_delim(argv[*no][0]) && argv[*no][0] != '&') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = argv[*no];
    argc++;
    *no += 1;
    ret = parseredirs(ret, no, argv);
  }
  cmd->argv[argc] = 0;
  return ret;
}

int peek(int *no, char** argv, char *str) {
  return argv[*no] != NULL && strchr(str, argv[*no][0]);
}

struct cmd* make_cmd(void) {
  struct execcmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd*)cmd;
}

struct cmd* make_redircmd(struct cmd *subcmd, char *file, int type) {
  struct redircmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = type;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->mode = (type == '<') ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
  cmd->fd = (type == '<') ? 0: 1;
  return (struct cmd*)cmd;
}

struct cmd* make_pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '|';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}


void token_clear(char** argv){
  int c = 0;
  while(argv[c] != NULL) {
    free(argv[c]);
    c++;
  }
}

void token_dump(char** argv){
  int c = 0;
  while(argv[c] != NULL) {
    printf("%s\n", argv[c]);
    c++;
  }
}

void cmd_dump(struct cmd* cmd) {
  int i;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0) return;

  switch(cmd->type) {
    default: 
      break;
    case ' ':
      ecmd = (struct execcmd *)cmd;
      for(i = 0; ecmd->argv[i] != 0 ; i++) {
        printf("%s ",ecmd->argv[i]);
      }
      break;
    case '<':
    case '>':
      rcmd = (struct redircmd *)cmd;
      printf("( ");
      cmd_dump(rcmd->cmd);
      printf("%c %s ", rcmd->fd ? '>' : '<', rcmd->file);
      printf(" )");
      break;
    case '|':
      pcmd = (struct pipecmd *)cmd;
      printf("( "); 
      cmd_dump(pcmd->left);
      printf(" ) | ");
      printf("( "); 
      cmd_dump(pcmd->right);
      printf(" )");
      break;
  }

}
