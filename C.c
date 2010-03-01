/* 
 * $Id: C.c,v 1.21 2006/05/19 05:18:41 labskazuho Exp $
 * 
 * C - a pseudo-interpreter of the C programming language
 * http://labs.cybozu.co.jp/blog/kazuhoatwork/
 * 
 * Copyright (C) 2006 Cybozu Labs, Inc.
 * 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */


#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>

#include <unistd.h>


#define VERSION_STR "0.06"
#define VERSION_INT_STR "0x00000500"

#ifndef FILES_PER_CACHEDIR
#define FILES_PER_CACHEDIR 128
#endif

char* root_dir;
char* store_dir;
char* temp_dir;
char* exec_file;
char* c_file;
char** src_lines;
FILE* src_fp;
int oneliner;
int use_debugger;
int use_main;
int use_plusplus;
int keep_files;
int show_disassembly;
char** gcc;
char** lopts;

char spec[65536];
int spec_size; // -1 if not used


static void cmd_error(char* fmt, ...);

static void add_spec(void* ptr, int size)
{
  if (spec_size == -1) {
    return;
  }
  if (sizeof(spec) < spec_size + size) {
    spec_size = -1;
    return;
  }
  memcpy(spec + spec_size, ptr, size);
  spec_size += size;
}

static char* str_dup(char* str)
{
  char* p;
  if ((p = malloc(strlen(str) + 1)) == NULL) {
    cmd_error("out of memory\n");
  }
  strcpy(p, str);
  return p;
}

static char* str_concat(char* base, char* add)
{
  if ((base = realloc(base, strlen(base) + strlen(add) + 1)) == NULL) {
    cmd_error("out of memory\n");
  }
  strcat(base, add);
  return base;
}
    
static void remove_dir(char* path)
{
  DIR* d;
  struct dirent* e;
  char* file;
  
  if ((d = opendir(path)) == NULL) {
    return;
  }
  while ((e = readdir(d)) != NULL) {
    if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
      file = str_concat(str_concat(str_dup(path), "/"), e->d_name);
      unlink(file);
      free(file);
    }
  }
  closedir(d);
  rmdir(path);
}

static void cleanup(void)
{
  if (! keep_files) {
    if (temp_dir != NULL) {
      remove_dir(temp_dir);
    }
  }
}

static void show_version(void)
{
  fputs("C " VERSION_STR "\n"
	"\n"
	"Copyright (C) 2006 Cybozu Labs, Inc.\n"
	"This is free software; see the source for copying conditions.  There is NO\n"
	"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
	"\n"
	"Written by Kazuho Oku (http://labs.cybozu.co.jp/blog/kazuhoatwork/)\n",
	stdout);
  exit(0);
}
	
static void usage(void)
{
  fputs("C  (pronounced  large-C)  is  a psuedo interpreter of the C programming\n"
	"language.\n"
	"\n"
	"Without the need of manual compilation, developers can  rapidly  create\n"
	"scripts  or write one-liners using the C programming language that runs\n"
	"at native-code speed.\n"
	"\n"
	"Usage: C [options] [sourcefile] [arguments]\n"
	"\n"
	"Options:\n"
	" -c<gcc_option>    pass a compiler option to GCC\n"
	" -d                use debugger\n"
	" -e <expression>   executes the expression\n"
	" -i<include_file>  add an include file\n"
	" -k                keep temporary files\n"
	" -l<gcc_option>    pass a linker option to GCC\n"
	" -m                use main function\n"
	" -p                use C++ (implies -m)\n"
	" -S                show disassembly\n"
	" -h, --help        displays this help message\n"
	" --version         displays version number\n"
	"\n"
	"Examples:\n"
	" % C -cWall -cO2 -e 'printf(\"hello world\\n\")'\n"
	" % C -p -e 'int main(int,char**) { cout << \"hello\" << endl; }'\n",
	stdout);
  exit(0);
}

void cmd_error(char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  if (fmt != NULL) {
    vfprintf(stderr, fmt, args);
  }
  va_end(args);
  
  if (src_fp != NULL) {
    fclose(src_fp);
  }
  cleanup();
  exit(255);
}

static void assert_cmdline(char* option, char* file, int line)
{
  if (file != NULL) {
    cmd_error("%s:%s: %s cannot be used in file\n", file, line, option);
  }
}

static void make_temp_dir(void)
{
#define MAX_TRIES 1000
  
  int rep;
  
  srand((int)time(NULL) ^ (int)getpid());
  
  for (rep = 0; rep < MAX_TRIES; rep++) {
    char randbuf[sizeof("/tmp/01234567")];
    sprintf(randbuf, "/tmp/%08x", rand());
    temp_dir = str_concat(str_dup(root_dir), randbuf);
    if (mkdir(temp_dir, 0777) == 0) {
      return;
    }
    free(temp_dir);
  }
  cmd_error("failed to create temporary directory.\n");
  
#undef MAX_TRIES
}

static void build_store_dir(void)
{
#define BASE 65521
  
  char buf[sizeof("/cache/01234567")];
  unsigned long s1 = 1;
  unsigned long s2 = 0;
  int n;
  
  for (n = 0; n < spec_size; n++) {
    s1 = (s1 + (unsigned char)spec[n]) % BASE;
    s2 = (s2 + s1) % BASE;
  }
  
  sprintf(buf, "/cache/%08lx", (s2 << 16) + s1);
  store_dir = str_concat(str_dup(root_dir), buf);
  
#undef BASE
}

static void update_cache(void)
{
  DIR* d = NULL;
  struct dirent* e;
  char* cache_root, * oldest_dir = NULL, * filename;
  time_t oldest_mtime = 0; // initialized here only to avoid compiler warning
  int num_files;
  
  num_files = 0;
  
  cache_root = str_concat(str_dup(root_dir), "/cache");
  if ((d = opendir(cache_root)) != NULL) {
    while ((e = readdir(d)) != NULL) {
      struct stat st;
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
	continue;
      }
      filename = str_concat(str_concat(str_dup(cache_root), "/"), e->d_name);
      if (stat(filename, &st) == 0 && S_ISDIR(st.st_mode)) {
	if (oldest_dir == NULL || st.st_mtime < oldest_mtime) {
	  free(oldest_dir);
	  oldest_dir = str_dup(filename);
	  oldest_mtime = st.st_mtime;
	}
	if (++num_files >= FILES_PER_CACHEDIR && oldest_dir != NULL) {
	  remove_dir(oldest_dir);
	  if (readdir(d) != NULL) {
	    /* more files to come, check again */
	    num_files = 0;
	    free(oldest_dir);
	    oldest_dir = NULL;
	    rewinddir(d);
	  } else {
	    free(filename);
	    break;
	  }
	}
      }
      free(filename);
    }
    closedir(d);
  }
  free(oldest_dir);
  free(cache_root);
}

static int check_specs(void)
{
  char* comp_file;
  FILE* fp;
  char comp_spec[sizeof(spec)];
  int equal = 0;
  
  comp_file = str_concat(str_dup(store_dir), "/SPECS");
  if ((fp = fopen(comp_file, "rb")) != NULL) {
    if (fread(comp_spec, 1, spec_size, fp) == spec_size &&
	fgetc(fp) == EOF &&
	memcmp(spec, comp_spec, spec_size) == 0) {
      equal = 1;
    }
    fclose(fp);
  }
  free(comp_file);
  
  return equal;
}

static void save_specs(void)
{
  char* filename;
  FILE* fp;
  
  filename = str_concat(str_dup(temp_dir), "/SPECS");
  if ((fp = fopen(filename, "wb")) == NULL) {
    cmd_error("failed to write file: %s : %s\n", filename, strerror(errno));
  }
  fwrite(spec, 1, spec_size, fp);
  fclose(fp);
  free(filename);
}

static int call_proc(char** argv, char* errmsg)
{
  pid_t pid;
  int status;

  /* fork */
  pid = fork();
  switch (pid) {
  case 0: /* child process */
    execvp(argv[0], argv);
    cmd_error("%s: %s : %s\n", errmsg, argv[0], strerror(errno));
    break;
  case -1: /* fork failed */
    cmd_error("%s: %s : %s\n", errmsg, argv[0], strerror(errno));
    break;
  }
  
  /* only the parent process reaches here, and only if fork succeeds */
 WAIT:
  if (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) goto WAIT;
    cmd_error("unexpected response from waitpid : %s\n", strerror(errno));
  }
  if (! WIFEXITED(status)) {
    cmd_error(NULL); /* silently exit(255) */
  }
  return WEXITSTATUS(status);
}


static char** sa_concat(char** x, char* y)
{
  int len = 0;
  if (x != NULL) {
    while (x[len] != NULL) {
      len++;
    }
  }
  if ((x = realloc(x, sizeof(char*) * (len + 2))) == NULL) {
    cmd_error("out of memory\n");
  }
  x[len++] = y;
  x[len++] = NULL;
  return x;
}

static char** sa_merge(char** x, char** y)
{
  if (y != NULL) {
    while (*y != NULL) {
      x = sa_concat(x, *y++);
    }
  }
  return x;
}

static char* get_line(FILE* fp)
{
  static char* buf = NULL;
  static int len = 0;
  
  int i = 0, ch;
  
  while ((ch = fgetc(fp)) != EOF) {
    if (i == len) {
      len += 4096;
      if ((buf = realloc(buf, len)) == NULL) {
	cmd_error("out of memory\n");
      }
    }
    buf[i++] = ch;
    if (ch == '\n') {
      break;
    }
  }
  if (i == 0) {
    return NULL;
  }
  buf[i] = '\0';
  
  return buf;
}

static char** split_tokens(char* line)
{
  char** tokens;
  
  if ((tokens = malloc(sizeof(char*))) == NULL) {
    cmd_error("out of memory\n");
  }
  tokens[0] = NULL;
  
  while (1) {
    for (; *line != '\0' && isspace((int)*line); line++)
      ;
    if (*line == '\0') {
      break;
    }
    tokens = sa_concat(tokens, line);
    for (line++; *line != '\0' && ! isspace((int)*line); line++)
      ;
    if (*line == '\0') {
      break;
    }
    *line++ = '\0';
  }
  
  return tokens;
}

static char** parse_args(char** argv, char* file, int line)
{
  while (*argv != NULL && argv[0][0] == '-') {
    char* arg = *argv++;
    if (strcmp(arg, "-") == 0) {
      argv--;
      break;
    } else if (strcmp(arg, "--") == 0) {
      break;
    } else if (strncmp(arg, "-c", 2) == 0 || strncmp(arg, "-l", 2) == 0) {
      char*** target = arg[1] == 'c' ? &gcc : &lopts;
      if (arg[2] == '\0') {
	if (*argv == NULL) {
	  cmd_error("%s not followed by a GCC argument\n", arg);
	}
	*target = sa_concat(*target, *argv++);
      } else {
	char* opt = str_dup(arg + 1);
	opt[0] = '-';
	*target = sa_concat(*target, opt);
      }
    } else if (strcmp(arg, "-d") == 0) {
      assert_cmdline(arg, file, line);
      gcc = sa_concat(gcc, "-g");
      use_debugger = 1;
    } else if (strcmp(arg, "-e") == 0) {
      assert_cmdline(arg, file, line);
      if (*argv == NULL) {
	cmd_error("-e should be followed by an expression\n");
      }
      if (oneliner) {
	cmd_error("multiple -e options not permitted.\n");
      }
      src_lines = sa_concat(src_lines, *argv++);
      src_lines = sa_concat(src_lines, ";\n");
      oneliner = 1;
    } else if (strncmp(arg, "-i", 2) == 0) {
      assert_cmdline(arg, file, line);
      src_lines = sa_concat(src_lines, "#include \"");
      src_lines = sa_concat(src_lines, arg + 2);
      src_lines = sa_concat(src_lines, "\"\n");
    } else if (strcmp(arg, "-k") == 0) {
      keep_files = 1;
    } else if (strcmp(arg, "-m") == 0) {
      use_main = 1;
    } else if (strcmp(arg, "-p") == 0) {
      use_main = 1;
      use_plusplus = 1;
    } else if (strcmp(arg, "-S") == 0) {
      gcc = sa_concat(gcc, "-S");
      show_disassembly = 1;
    } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      assert_cmdline(arg, file, line);
      usage();
    } else if (strcmp(arg, "--version") == 0) {
      show_version();
    } else {
      cmd_error("unknown option: %s\n", arg);
    }
  }
  if (file != NULL && *argv != NULL) {
    cmd_error("invalid option: %s\n", *argv);
  }
  return argv;
}

void setup_dir(void)
{
  char* dirname, buf[sizeof("/LARGE_C-01234567")];
  uid_t euid = geteuid();
  int old_umask = -1;
  
  /* use TMPDIR, or if not set, create a directory of my own under P_tmpdir */
  if ((root_dir = getenv("TMPDIR")) != NULL) {
    root_dir = str_dup(root_dir);
  } else {
    old_umask = umask(077);
    sprintf(buf, "/LARGE_C-%u", (unsigned)euid);
    root_dir = str_concat(str_dup(P_tmpdir), buf);
    if (mkdir(root_dir, 0700) != 0) {
      struct stat st;
      if (lstat(root_dir, &st) != 0) {
	cmd_error("failed to stat: %s : %s\n", root_dir, strerror(errno));
      }
      if (st.st_uid != euid) {
	cmd_error("%s owned by somebody else\n", root_dir);
      }
    }
  }
  /* make subdirs */
  dirname = str_concat(str_dup(root_dir), "/cache");
  mkdir(dirname, 0777);
  free(dirname);
  dirname = str_concat(str_dup(root_dir), "/tmp");
  mkdir(dirname, 0777);
  free(dirname);
  
  /* restore */
  if (old_umask != -1) {
    umask(old_umask);
  }
}

int main(int argc, char** argv)
{
  int ret;
  
  setup_dir();
  
  /* init globals */
  gcc = sa_concat(gcc, "gcc"); /* replaced laterwards if c++ */
  gcc = sa_concat(gcc, "-I.");
  
  src_lines =
    sa_concat(src_lines,
	      "#define __LARGE_C__ " VERSION_INT_STR "\n"
	      "#ifdef __cplusplus\n"
	      "extern \"C\" {\n"
	      "#endif\n"
	      "#include <stdio.h>\n"
	      "#include <stdlib.h>\n"
	      "#ifdef __cplusplus\n"
	      "}\n"
	      "#include <iostream>\n"
	      "using namespace std;\n"
	      "#endif\n"
	      "\n"
	      "__LARGE_C_PREFIX__\n");
  
  argv++;
  { /* parse args, determine cache dir */
    char** new_argv = parse_args(argv, NULL, 0);
    for (; argv != new_argv; argv++) {
      add_spec(*argv, strlen(*argv) + 1);
    }
    if (! keep_files && (oneliner || *argv != NULL)) {
      struct stat st;
      if (oneliner) {
	build_store_dir();
      } else if (stat(*argv, &st) == 0) {
	add_spec(*argv, strlen(*argv) + 1);
	add_spec(&st.st_size, sizeof(st.st_size));
	add_spec(&st.st_mtime, sizeof(st.st_mtime));
	build_store_dir();
      }
    }
  }
  
  /* use cache if possible */
  if (store_dir != NULL && check_specs()) {
    char** child_argv = NULL;
    utimes(store_dir, NULL); /* update mtime of the directory */
    exec_file = str_concat(str_dup(store_dir), "/a.out");
    child_argv = sa_concat(child_argv, exec_file);
    child_argv = sa_merge(child_argv, argv + 1);
    execv(exec_file, child_argv);
    // if execv failed, we compile
    free(exec_file);
    remove_dir(store_dir);
  }
  
  /* prepare files */
  make_temp_dir();
  exec_file = str_concat(str_dup(temp_dir), "/a.out");
  c_file = str_concat(str_dup(temp_dir), "/source.c");
  if ((src_fp = fopen(c_file, "wt")) == NULL) {
    cmd_error("failed to create temporary file: %s : %s\n", c_file,
	      strerror(errno));
  }
  while (src_lines != NULL && *src_lines != NULL) {
    fputs(*src_lines++, src_fp);
  }
  
  /* write source with adjustments */
  if (! oneliner) {
    FILE* fp;
    char* file;
    char* line;
    int line_no = 0;
    if (argv[0] == NULL) {
      fp = stdin;
      file = "stdin";
    } else if (strcmp(argv[0], "-") == 0) {
      fp = stdin;
      argv++;
      file = "stdin";
    } else {
      file = *argv++;
      if ((fp = fopen(file, "rt")) == NULL) {
	cmd_error("cannot open file: %s : %s\n", file, strerror(errno));
      }
      fprintf(src_fp, "# 1 \"%s\" 1\n", file);
    }
    while ((line = get_line(fp)) != NULL) {
      int comment_out = 0;
      line_no++;
      if (line_no == 1 && strncmp(line, "#!", 2) == 0) {
	comment_out = 1;
      } else if (line[0] == '#') {
	char* buf = str_dup(line + 1);
	char** tokens = split_tokens(buf);
	if (*tokens != NULL) {
	  if (strcmp(tokens[0], "option") == 0) {
	    parse_args(tokens + 1, file, line_no);
	    comment_out = 1;
	  }
	}
	free(buf);
	free(tokens);
      }
      if (comment_out == 1) {
	fprintf(src_fp, "// ");
      }
      fputs(line, src_fp);
    }
    fputs("\n", src_fp);
    if (fp != stdin) {
      fclose(fp);
    }
  }
  
  /* close source file */
  fputs("__LARGE_C_SUFFIX__\n", src_fp);
  fclose(src_fp);
  src_fp = NULL;
  
  /* compile */
  if (use_plusplus) {
    gcc[0] = "g++";
  }
  if (use_main) {
    gcc = sa_concat(gcc, "-D__LARGE_C_PREFIX__=");
    gcc = sa_concat(gcc, "-D__LARGE_C_SUFFIX__=");
  } else {
    gcc =
      sa_concat(gcc, "-D__LARGE_C_PREFIX__=int main(int argc, char** argv) {");
    gcc = sa_concat(gcc, "-D__LARGE_C_SUFFIX__=; return 0; }");
  }
  gcc = sa_concat(gcc, "-o");
  gcc = sa_concat(gcc, show_disassembly ? "-" : exec_file);
  gcc = sa_concat(gcc, c_file);
  gcc = sa_merge(gcc, lopts);
  if ((ret = call_proc(gcc, "could not execute compiler")) != 0) {
    cleanup();
    exit(ret);
  }
  
  if (show_disassembly) {
    cleanup();
    exit(0);
  }
  
  { /* execute */
    char** child_argv = NULL;
    if (use_debugger) {
      child_argv = sa_concat(child_argv, "gdb");
    }
    child_argv = sa_concat(child_argv, exec_file);
    child_argv = sa_merge(child_argv, argv);
    ret = call_proc(child_argv, "could not spawn child process");
  }
  
  /* move temp_dir to store_dir, if possible.
   * or, remove work_dir
   */
  if (store_dir == NULL) {
    cleanup();
  } else {
    save_specs();
    update_cache();
    if (rename(temp_dir, store_dir) != 0) {
      cleanup();
    }
  }
  
  return ret;
}
