#ifndef BC2_ABI_BRIDGE_H
#define BC2_ABI_BRIDGE_H

#include <dirent.h>
#include <stdint.h>
#include <time.h>

#include <NovaGL.h>

#define BC2_SOFTFP __attribute__((pcs("aapcs")))

typedef int (*bc2_softfp_joystick_fn)(int type, float x, float y, int id)
    BC2_SOFTFP;

void BC2_SOFTFP bc2_glAlphaFunc(GLenum func, GLfloat ref);
void BC2_SOFTFP bc2_glClearColor(GLfloat red, GLfloat green, GLfloat blue,
                                 GLfloat alpha);
void BC2_SOFTFP bc2_glClearDepthf(GLfloat depth);
void BC2_SOFTFP bc2_glDepthRangef(GLfloat near_value, GLfloat far_value);
void BC2_SOFTFP bc2_glFogf(GLenum pname, GLfloat param);
void BC2_SOFTFP bc2_glTexEnvf(GLenum target, GLenum pname, GLfloat param);

double BC2_SOFTFP bc2_acos(double value);
double BC2_SOFTFP bc2_asin(double value);
double BC2_SOFTFP bc2_atan(double value);
double BC2_SOFTFP bc2_atan2(double y, double x);
double BC2_SOFTFP bc2_ceil(double value);
double BC2_SOFTFP bc2_cos(double value);
double BC2_SOFTFP bc2_floor(double value);
double BC2_SOFTFP bc2_fmod(double x, double y);
double BC2_SOFTFP bc2_ldexp(double x, int exponent);
double BC2_SOFTFP bc2_log(double value);
double BC2_SOFTFP bc2_pow(double x, double y);
double BC2_SOFTFP bc2_sin(double value);
double BC2_SOFTFP bc2_sqrt(double value);
double BC2_SOFTFP bc2_tan(double value);

typedef int32_t bionic_time_t;

struct bionic_timeval {
  int32_t tv_sec;
  int32_t tv_usec;
};

struct bionic_stat {
  uint64_t dev;
  uint8_t pad0[4];
  uint32_t ino32;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint64_t rdev;
  uint8_t pad3[4];
  int64_t size;
  uint32_t blksize;
  uint64_t blocks;
  uint32_t atime_sec;
  uint32_t atime_nsec;
  uint32_t mtime_sec;
  uint32_t mtime_nsec;
  uint32_t ctime_sec;
  uint32_t ctime_nsec;
  uint64_t ino64;
};

struct bionic_dirent {
  uint64_t d_ino;
  int64_t d_off;
  uint16_t d_reclen;
  uint8_t d_type;
  char d_name[256];
};

bionic_time_t bc2_time(bionic_time_t *out);
struct tm *bc2_localtime(const bionic_time_t *timer);
double BC2_SOFTFP bc2_difftime(bionic_time_t end, bionic_time_t beginning);
int bc2_gettimeofday(struct bionic_timeval *tv, void *tz);
int bc2_fstat(int fd, struct bionic_stat *out);
int bc2_lstat(const char *path, struct bionic_stat *out);
struct bionic_dirent *bc2_readdir(DIR *dir);

#endif
