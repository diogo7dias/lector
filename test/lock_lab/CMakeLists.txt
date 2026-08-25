add_executable(LockLabAuditTest
  LockLabAuditTest.cpp
)

target_compile_definitions(LockLabAuditTest PRIVATE
  LOCK_LAB_SOURCES="${REPO_ROOT}/src/dev/LockLab.h|${REPO_ROOT}/src/dev/LockLab.cpp|${REPO_ROOT}/src/dev/LockLabState.h|${REPO_ROOT}/src/dev/LockLabActivity.h|${REPO_ROOT}/src/dev/LockLabActivity.cpp|${REPO_ROOT}/src/main.cpp|${REPO_ROOT}/src/CrossPointState.h|${REPO_ROOT}/src/CrossPointState.cpp|${REPO_ROOT}/src/activities/ActivityManager.h|${REPO_ROOT}/src/activities/ActivityManager.cpp|${REPO_ROOT}/src/activities/home/HomeActivity.h|${REPO_ROOT}/src/activities/home/HomeActivity.cpp|${REPO_ROOT}/src/activities/boot_sleep/SleepActivity.cpp|${REPO_ROOT}/src/activities/boot_sleep/PxcSleepRenderer.cpp"
)

target_link_libraries(LockLabAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LockLabAuditTest)
