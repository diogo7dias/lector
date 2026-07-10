#include <gtest/gtest.h>

#include <cstddef>
#include <new>

#include "BitmapHelpers.h"

namespace {

int allocationCalls = 0;
int failAtCall = 0;

int16_t* failSelectedAllocation(const size_t count) {
  allocationCalls++;
  if (allocationCalls == failAtCall) return nullptr;
  return new (std::nothrow) int16_t[count]();
}

template <typename Ditherer>
void expectAllocationFailureIsReported(const int allocationToFail) {
  allocationCalls = 0;
  failAtCall = allocationToFail;
  Ditherer ditherer(480, failSelectedAllocation);
  EXPECT_FALSE(ditherer.isValid());
}

}  // namespace

TEST(DitherMemory, Atkinson1BitReportsEveryRowAllocationFailure) {
  expectAllocationFailureIsReported<Atkinson1BitDitherer>(1);
  expectAllocationFailureIsReported<Atkinson1BitDitherer>(2);
  expectAllocationFailureIsReported<Atkinson1BitDitherer>(3);
}

TEST(DitherMemory, AtkinsonReportsEveryRowAllocationFailure) {
  expectAllocationFailureIsReported<AtkinsonDitherer>(1);
  expectAllocationFailureIsReported<AtkinsonDitherer>(2);
  expectAllocationFailureIsReported<AtkinsonDitherer>(3);
}

TEST(DitherMemory, FloydSteinbergReportsEveryRowAllocationFailure) {
  expectAllocationFailureIsReported<FloydSteinbergDitherer>(1);
  expectAllocationFailureIsReported<FloydSteinbergDitherer>(2);
}

TEST(DitherMemory, RejectsInvalidWidthsWithoutAllocating) {
  allocationCalls = 0;
  failAtCall = 99;
  AtkinsonDitherer zero(0, failSelectedAllocation);
  FloydSteinbergDitherer negative(-1, failSelectedAllocation);
  EXPECT_FALSE(zero.isValid());
  EXPECT_FALSE(negative.isValid());
  EXPECT_EQ(allocationCalls, 0);
}
