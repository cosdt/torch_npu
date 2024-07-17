#pragma once

#include <c10/macros/Macros.h>
#include <npu/acl/include/acl/acl_base.h>
#include <unistd.h>
#include <chrono>
#include <cstdarg>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include "csrc/core/Exception.h"
#include "npu/core/NPUErrorCodes.h"
#include "npu/core/NPUMacros.h"
#include "npu/core/interface/AclInterface.h"

#define C10_NPU_SHOW_ERR_MSG()                                      \
  do {                                                              \
    std::cout << c10_npu::c10_npu_get_error_message() << std::endl; \
  } while (0)

#define NPU_CHECK_WARN(err_code)                 \
  do {                                           \
    auto Error = err_code;                       \
    static c10_npu::acl::AclErrorCode err_map;   \
    if ((Error) != ACL_ERROR_NONE) {             \
      TORCH_BACKEND_FORMAT_WARN(                 \
          err_code,                              \
          err_map.error_code_map,                \
          c10_npu::c10_npu_get_error_message()); \
    }                                            \
  } while (0)

#define TORCH_NPU_WARN(...) TORCH_BACKEND_WARN(__VA_ARGS__)

#define TORCH_NPU_WARN_ONCE(...) TORCH_BACKEND_WARN_ONCE(__VA_ARGS__)

enum class SubModule { PTA = 0, OPS = 1, DIST = 2, GRAPH = 3, PROF = 4 };

enum class ErrCode {
  SUC = 0,
  PARAM = 1,
  TYPE = 2,
  VALUE = 3,
  PTR = 4,
  INTERNAL = 5,
  MEMORY = 6,
  NOT_SUPPORT = 7,
  NOT_FOUND = 8,
  UNAVAIL = 9,
  SYSCALL = 10,
  TIMEOUT = 11,
  PERMISSION = 12,
  ACL = 100,
  HCCL = 200,
  GE = 300
};

static std::string getCurrentTimestamp();
std::string formatErrorCode(SubModule submodule, ErrCode errorCode);

#define PTA_ERROR(error) formatErrorCode(SubModule::PTA, error)
#define OPS_ERROR(error) formatErrorCode(SubModule::OPS, error)
#define DIST_ERROR(error) formatErrorCode(SubModule::DIST, error)
#define GRAPH_ERROR(error) formatErrorCode(SubModule::GRAPH, error)
#define PROF_ERROR(error) formatErrorCode(SubModule::PROF, error)

inline const char* getErrorFunction(const char* msg) {
  return msg;
}

// If there is just 1 provided C-string argument, use it.
inline const char* getErrorFunction(const char* /* msg */, const char* args) {
  return args;
}

#define NPU_CHECK_ERROR(err_code, ...)                \
  do {                                                \
    auto Error = err_code;                            \
    static c10_npu::acl::AclErrorCode err_map;        \
    if ((Error) != ACL_ERROR_NONE) {                  \
      TORCH_BACKEND_FORMAT_ERROR(                     \
          err_code,                                   \
          err_map.error_code_map,                     \
          " NPU function error: ",                    \
          getErrorFunction(#err_code, ##__VA_ARGS__), \
          PTA_ERROR(ErrCode::ACL),                    \
          c10_npu::c10_npu_get_error_message());      \
    }                                                 \
  } while (0)

#define OPS_CHECK_ERROR(err_code, ...)                \
  do {                                                \
    auto Error = err_code;                            \
    static c10_npu::acl::AclErrorCode err_map;        \
    if ((Error) != ACL_ERROR_NONE) {                  \
      TORCH_BACKEND_FORMAT_ERROR(                     \
          err_code,                                   \
          err_map.error_code_map,                     \
          " OPS function error: ",                    \
          getErrorFunction(#err_code, ##__VA_ARGS__), \
          OPS_ERROR(ErrCode::ACL),                    \
          c10_npu::c10_npu_get_error_message())       \
    }                                                 \
  } while (0)

#define NPU_CHECK_SUPPORTED_OR_ERROR(err_code)                      \
  do {                                                              \
    auto Error = err_code;                                          \
    static c10_npu::acl::AclErrorCode err_map;                      \
    if ((Error) != ACL_ERROR_NONE) {                                \
      if ((Error) == ACL_ERROR_RT_FEATURE_NOT_SUPPORT) {            \
        static auto feature_not_support_warn_once = []() {          \
          printf(                                                   \
              "[WARN]%s,%s:%u:%s\n",                                \
              __FUNCTION__,                                         \
              __FILENAME__,                                         \
              __LINE__,                                             \
              "Feature is not supportted and the possible cause is" \
              " that driver and firmware packages do not match.");  \
          return true;                                              \
        }();                                                        \
      } else {                                                      \
        TORCH_BACKEND_FORMAT_ERROR(                                 \
            err_code,                                               \
            err_map.error_code_map,                                 \
            c10_npu::c10_npu_get_error_message());                  \
      }                                                             \
    }                                                               \
  } while (0)
namespace c10_npu {

C10_NPU_API const char* c10_npu_get_error_message();

} // namespace c10_npu
