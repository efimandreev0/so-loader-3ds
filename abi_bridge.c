#include <math.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "abi_bridge.h"

_Static_assert(sizeof(struct bionic_stat) == 104, "bionic stat layout");
_Static_assert(offsetof(struct bionic_stat, size) == 48, "bionic stat layout");
_Static_assert(offsetof(struct bionic_stat, mtime_sec) == 80, "bionic stat layout");
_Static_assert(offsetof(struct bionic_dirent, d_name) == 19, "bionic dirent layout");

void BC2_SOFTFP bc2_glAlphaFunc(GLenum func, GLfloat ref) {
  glAlphaFunc(func, ref);
}

void BC2_SOFTFP bc2_glClearColor(GLfloat red, GLfloat green, GLfloat blue,
                                 GLfloat alpha) {
  glClearColor(red, green, blue, alpha);
}

void BC2_SOFTFP bc2_glClearDepthf(GLfloat depth) {
  glClearDepthf(depth);
}

void BC2_SOFTFP bc2_glDepthRangef(GLfloat near_value, GLfloat far_value) {
  glDepthRangef(near_value, far_value);
}

void BC2_SOFTFP bc2_glFogf(GLenum pname, GLfloat param) {
  glFogf(pname, param);
}

void BC2_SOFTFP bc2_glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
  glTexEnvf(target, pname, param);
}

double BC2_SOFTFP bc2_acos(double value) { return acos(value); }
double BC2_SOFTFP bc2_asin(double value) { return asin(value); }
double BC2_SOFTFP bc2_atan(double value) { return atan(value); }
double BC2_SOFTFP bc2_atan2(double y, double x) { return atan2(y, x); }
double BC2_SOFTFP bc2_ceil(double value) { return ceil(value); }
double BC2_SOFTFP bc2_cos(double value) { return cos(value); }
double BC2_SOFTFP bc2_difftime(bionic_time_t end, bionic_time_t beginning) {
  return difftime((time_t)end, (time_t)beginning);
}
double BC2_SOFTFP bc2_floor(double value) { return floor(value); }
double BC2_SOFTFP bc2_fmod(double x, double y) { return fmod(x, y); }
double BC2_SOFTFP bc2_ldexp(double x, int exponent) { return ldexp(x, exponent); }
double BC2_SOFTFP bc2_log(double value) { return log(value); }
double BC2_SOFTFP bc2_pow(double x, double y) { return pow(x, y); }
double BC2_SOFTFP bc2_sin(double value) { return sin(value); }
double BC2_SOFTFP bc2_sqrt(double value) { return sqrt(value); }
double BC2_SOFTFP bc2_tan(double value) { return tan(value); }

bionic_time_t bc2_time(bionic_time_t *out) {
  bionic_time_t now = (bionic_time_t)time(NULL);
  if (out != NULL)
    *out = now;
  return now;
}

struct tm *bc2_localtime(const bionic_time_t *timer) {
  time_t value = timer != NULL ? (time_t)*timer : 0;
  struct tm *result = localtime(&value);
  if (result != NULL)
    return result;
  /* The library has an uninitialised-time path; mirror the Vita loader and
   * answer with the current time instead of NULL. */
  value = time(NULL);
  return localtime(&value);
}

int bc2_gettimeofday(struct bionic_timeval *tv, void *tz) {
  struct timeval native;
  int result = gettimeofday(&native, NULL);

  (void)tz;
  if (result == 0 && tv != NULL) {
    tv->tv_sec = (int32_t)native.tv_sec;
    tv->tv_usec = (int32_t)native.tv_usec;
  }
  return result;
}

static void stat_to_bionic(const struct stat *in, struct bionic_stat *out) {
  memset(out, 0, sizeof(*out));
  out->dev = (uint64_t)in->st_dev;
  out->ino32 = (uint32_t)in->st_ino;
  out->ino64 = (uint64_t)in->st_ino;
  out->mode = (uint32_t)in->st_mode;
  out->nlink = (uint32_t)in->st_nlink;
  out->uid = (uint32_t)in->st_uid;
  out->gid = (uint32_t)in->st_gid;
  out->rdev = (uint64_t)in->st_rdev;
  out->size = (int64_t)in->st_size;
  out->blksize = (uint32_t)in->st_blksize;
  out->blocks = (uint64_t)in->st_blocks;
  out->atime_sec = (uint32_t)in->st_atime;
  out->mtime_sec = (uint32_t)in->st_mtime;
  out->ctime_sec = (uint32_t)in->st_ctime;
}

int bc2_fstat(int fd, struct bionic_stat *out) {
  struct stat st;
  int result = fstat(fd, &st);
  if (result == 0 && out != NULL)
    stat_to_bionic(&st, out);
  return result;
}

int bc2_lstat(const char *path, struct bionic_stat *out) {
  /* Symbolic links do not exist on the SD filesystem. */
  struct stat st;
  int result = stat(path, &st);
  if (result == 0 && out != NULL)
    stat_to_bionic(&st, out);
  return result;
}

struct bionic_dirent *bc2_readdir(DIR *dir) {
  static struct bionic_dirent entry;
  struct dirent *native = readdir(dir);

  if (native == NULL)
    return NULL;
  memset(&entry, 0, sizeof(entry));
  entry.d_ino = (uint64_t)native->d_ino;
  entry.d_reclen = sizeof(entry);
  entry.d_type = native->d_type; /* newlib uses the Linux DT_* values. */
  strncpy(entry.d_name, native->d_name, sizeof(entry.d_name) - 1);
  return &entry;
}
