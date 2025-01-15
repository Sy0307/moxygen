/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "moxygen/dejitter/DeJitter.h"
#include <folly/io/IOBuf.h>
#include <folly/portability/GTest.h>

using namespace moxygen::dejitter;

TEST(DeJitterTest, NoGapsUniquePrt) {
  DeJitter<std::unique_ptr<folly::IOBuf>> dejitter(3);
  folly::IOBufEqualTo eq;
  auto bufA = folly::IOBuf::copyBuffer("a");
  auto bufB = folly::IOBuf::copyBuffer("b");
  auto bufC = folly::IOBuf::copyBuffer("c");
  auto bufD = folly::IOBuf::copyBuffer("d");

  auto r0 = dejitter.insertItem(0, bufA->clone());
  EXPECT_FALSE(std::get<0>(r0).has_value());
  EXPECT_EQ(std::get<1>(r0).gapSize, 0);
  EXPECT_EQ(
      std::get<1>(r0).gapType,
      DeJitter<std::unique_ptr<folly::IOBuf>>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 1);

  auto r1 = dejitter.insertItem(1, bufB->clone());
  EXPECT_FALSE(std::get<0>(r1).has_value());
  EXPECT_EQ(std::get<1>(r1).gapSize, 0);
  EXPECT_EQ(
      std::get<1>(r1).gapType,
      DeJitter<std::unique_ptr<folly::IOBuf>>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 2);

  auto r2 = dejitter.insertItem(2, bufC->clone());
  EXPECT_FALSE(std::get<0>(r2).has_value());
  EXPECT_EQ(std::get<1>(r2).gapSize, 0);
  EXPECT_EQ(
      std::get<1>(r2).gapType,
      DeJitter<std::unique_ptr<folly::IOBuf>>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 3);

  auto r3 = dejitter.insertItem(3, bufD->clone());
  EXPECT_TRUE(std::get<0>(r3).has_value());
  EXPECT_TRUE(eq(std::get<0>(r3).value(), bufA));
  EXPECT_EQ(std::get<1>(r3).gapSize, 0);
  EXPECT_EQ(
      std::get<1>(r3).gapType,
      DeJitter<std::unique_ptr<folly::IOBuf>>::GapType::NO_GAP);
  EXPECT_EQ(dejitter.size(), 3);
}

TEST(DeJitterTest, OutOfOrder) {
  DeJitter<int> dejitter(3);

  auto r1 = dejitter.insertItem(1, 1);
  EXPECT_FALSE(std::get<0>(r1).has_value());
  EXPECT_EQ(std::get<1>(r1).gapSize, 0);
  EXPECT_EQ(std::get<1>(r1).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 1);

  auto r2 = dejitter.insertItem(2, 2);
  EXPECT_FALSE(std::get<0>(r2).has_value());
  EXPECT_EQ(std::get<1>(r2).gapSize, 0);
  EXPECT_EQ(std::get<1>(r2).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 2);

  auto r3 = dejitter.insertItem(3, 3);
  EXPECT_FALSE(std::get<0>(r3).has_value());
  EXPECT_EQ(std::get<1>(r3).gapSize, 0);
  EXPECT_EQ(std::get<1>(r3).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 3);

  auto r0 = dejitter.insertItem(0, 0);
  EXPECT_TRUE(std::get<0>(r0).has_value());
  EXPECT_EQ(std::get<0>(r0).value(), 0);
  EXPECT_EQ(std::get<1>(r0).gapSize, 0);
  EXPECT_EQ(std::get<1>(r0).gapType, DeJitter<int>::GapType::NO_GAP);
  EXPECT_EQ(dejitter.size(), 3);

  auto r4 = dejitter.insertItem(4, 4);
  EXPECT_TRUE(std::get<0>(r4).has_value());
  EXPECT_EQ(std::get<0>(r4).value(), 1);
  EXPECT_EQ(std::get<1>(r4).gapSize, 0);
  EXPECT_EQ(std::get<1>(r4).gapType, DeJitter<int>::GapType::NO_GAP);
  EXPECT_EQ(dejitter.size(), 3);
}

TEST(DeJitterTest, GapOfOne) {
  DeJitter<int> dejitter(3);

  auto r2 = dejitter.insertItem(2, 2);
  EXPECT_FALSE(std::get<0>(r2).has_value());
  EXPECT_EQ(std::get<1>(r2).gapSize, 0);
  EXPECT_EQ(std::get<1>(r2).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 1);

  auto r1 = dejitter.insertItem(0, 0);
  EXPECT_FALSE(std::get<0>(r1).has_value());
  EXPECT_EQ(std::get<1>(r1).gapSize, 0);
  EXPECT_EQ(std::get<1>(r1).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 2);

  auto r3 = dejitter.insertItem(3, 3);
  EXPECT_FALSE(std::get<0>(r3).has_value());
  EXPECT_EQ(std::get<1>(r3).gapSize, 0);
  EXPECT_EQ(std::get<1>(r3).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 3);

  auto r4 = dejitter.insertItem(4, 4);
  EXPECT_TRUE(std::get<0>(r4).has_value());
  EXPECT_EQ(std::get<0>(r4).value(), 0);
  EXPECT_EQ(std::get<1>(r4).gapSize, 0);
  EXPECT_EQ(std::get<1>(r4).gapType, DeJitter<int>::GapType::NO_GAP);
  EXPECT_EQ(dejitter.size(), 3);

  auto r5 = dejitter.insertItem(5, 5);
  EXPECT_TRUE(std::get<0>(r5).has_value());
  EXPECT_EQ(std::get<0>(r5).value(), 2);
  EXPECT_EQ(std::get<1>(r5).gapSize, 1);
  EXPECT_EQ(std::get<1>(r5).gapType, DeJitter<int>::GapType::GAP);
  EXPECT_EQ(dejitter.size(), 3);
}

TEST(DeJitterTest, GapOfTwo) {
  DeJitter<int> dejitter(3);

  auto r3 = dejitter.insertItem(3, 3);
  EXPECT_FALSE(std::get<0>(r3).has_value());
  EXPECT_EQ(std::get<1>(r3).gapSize, 0);
  EXPECT_EQ(std::get<1>(r3).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 1);

  auto r1 = dejitter.insertItem(0, 0);
  EXPECT_FALSE(std::get<0>(r1).has_value());
  EXPECT_EQ(std::get<1>(r1).gapSize, 0);
  EXPECT_EQ(std::get<1>(r1).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 2);

  auto r4 = dejitter.insertItem(4, 4);
  EXPECT_FALSE(std::get<0>(r4).has_value());
  EXPECT_EQ(std::get<1>(r4).gapSize, 0);
  EXPECT_EQ(std::get<1>(r4).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 3);

  auto r5 = dejitter.insertItem(5, 5);
  EXPECT_TRUE(std::get<0>(r5).has_value());
  EXPECT_EQ(std::get<0>(r5).value(), 0);
  EXPECT_EQ(std::get<1>(r5).gapSize, 0);
  EXPECT_EQ(std::get<1>(r5).gapType, DeJitter<int>::GapType::NO_GAP);
  EXPECT_EQ(dejitter.size(), 3);

  auto r6 = dejitter.insertItem(6, 6);
  EXPECT_TRUE(std::get<0>(r6).has_value());
  EXPECT_EQ(std::get<0>(r6).value(), 3);
  EXPECT_EQ(std::get<1>(r6).gapSize, 2);
  EXPECT_EQ(std::get<1>(r6).gapType, DeJitter<int>::GapType::GAP);
  EXPECT_EQ(dejitter.size(), 3);
}

TEST(DeJitterTest, ArriveLate) {
  DeJitter<int> dejitter(3);

  auto r3 = dejitter.insertItem(3, 3);
  EXPECT_FALSE(std::get<0>(r3).has_value());
  EXPECT_EQ(std::get<1>(r3).gapSize, 0);
  EXPECT_EQ(std::get<1>(r3).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 1);

  auto r4 = dejitter.insertItem(4, 4);
  EXPECT_FALSE(std::get<0>(r4).has_value());
  EXPECT_EQ(std::get<1>(r4).gapSize, 0);
  EXPECT_EQ(std::get<1>(r4).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 2);

  auto r5 = dejitter.insertItem(5, 5);
  EXPECT_FALSE(std::get<0>(r5).has_value());
  EXPECT_EQ(std::get<1>(r5).gapSize, 0);
  EXPECT_EQ(std::get<1>(r5).gapType, DeJitter<int>::GapType::FILLING_BUFFER);
  EXPECT_EQ(dejitter.size(), 3);

  auto r6 = dejitter.insertItem(6, 6);
  EXPECT_TRUE(std::get<0>(r6).has_value());
  EXPECT_EQ(std::get<0>(r6).value(), 3);
  EXPECT_EQ(
      std::get<1>(r6).gapSize, 0); // Is first, so NO GAP even if starts at 3
  EXPECT_EQ(std::get<1>(r6).gapType, DeJitter<int>::GapType::NO_GAP);
  EXPECT_EQ(dejitter.size(), 3);

  // Late
  auto r0 = dejitter.insertItem(0, 0);
  EXPECT_FALSE(std::get<0>(r0).has_value());
  EXPECT_EQ(std::get<1>(r0).gapSize, 3); // Arrived 3 late
  EXPECT_EQ(std::get<1>(r0).gapType, DeJitter<int>::GapType::ARRIVED_LATE);
  EXPECT_EQ(dejitter.size(), 3);
}
