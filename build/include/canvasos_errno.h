#pragma once
/* canvasos_errno.h — Phase-11 Sprint 1: Unified Error Codes */
#include <stdint.h>

typedef enum {
    COS_OK        =   0,
    COS_EPERM     =  -1,
    COS_ENOENT    =  -2,
    COS_ESRCH     =  -3,
    COS_EINTR     =  -4,
    COS_ENOSYS    =  -5,
    COS_ENOMEM    =  -6,
    COS_EBADF     =  -7,
    COS_EEXIST    =  -8,
    COS_EACCES    =  -9,
    COS_EPIPE     = -10,
} CosError;

static inline const char *cos_strerror(int err) {
    switch (err) {
    case COS_OK:      return "OK";
    case COS_EPERM:   return "EPERM";
    case COS_ENOENT:  return "ENOENT";
    case COS_ESRCH:   return "ESRCH";
    case COS_EINTR:   return "EINTR";
    case COS_ENOSYS:  return "ENOSYS";
    case COS_ENOMEM:  return "ENOMEM";
    case COS_EBADF:   return "EBADF";
    case COS_EEXIST:  return "EEXIST";
    case COS_EACCES:  return "EACCES";
    case COS_EPIPE:   return "EPIPE";
    default:          return "UNKNOWN";
    }
}
