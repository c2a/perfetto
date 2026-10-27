/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/importers/common/args_tracker.h"

#include <iterator>
#include <limits>
#include <string>

#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/args_utils.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_processor {
namespace {

TEST(ArgsTrackerTest, ExtendSharedArgSetPreservesTypesAndFlatKeys) {
  TraceStorage storage;
  ArgsTracker tracker(&storage);
  ArgsTracker::CompactArgSet original;
  Variadic values[] = {
      Variadic::Integer(-3),
      Variadic::UnsignedInteger(std::numeric_limits<uint64_t>::max()),
      Variadic::String(storage.InternString("value")),
      Variadic::Real(1.25),
      Variadic::Pointer(42),
      Variadic::Boolean(true),
      Variadic::Json(storage.InternString("{}")),
      Variadic::Null()};
  for (uint32_t i = 0; i < std::size(values); ++i) {
    StringId key = storage.InternString("value[" + std::to_string(i) + "]");
    original.emplace_back(
        ArgsTracker::CompactArg{storage.InternString("value"), key, values[i]});
  }
  ArgSetId base = tracker.AddArgSet(original.data(),
                                    static_cast<uint32_t>(original.size()));
  tables::TrackTable::Row row;
  row.dimension_arg_set_id = base;
  TrackId first = storage.mutable_track_table()->Insert(row).id;
  TrackId second = storage.mutable_track_table()->Insert(row).id;

  StringId key = storage.InternString("custom_dimension.rank");
  ArgsTracker::CompactArg extra{key, key, Variadic::Integer(7)};
  ArgSetId extended = tracker.ExtendArgSet(base, &extra, 1);
  (*storage.mutable_track_table())[first].set_dimension_arg_set_id(extended);
  ASSERT_NE(extended, base);
  EXPECT_EQ(storage.track_table()[second].dimension_arg_set_id(), base);

  ArgExtractor extractor(storage.arg_table());
  for (ArgSetId id : {base, extended}) {
    for (const auto& arg : original) {
      ASSERT_NE(extractor.Get(id, storage.GetString(arg.key).c_str()),
                std::numeric_limits<uint32_t>::max());
      EXPECT_EQ(extractor.cursor().flat_key(), arg.flat_key);
      EXPECT_EQ(GetArgValue(storage, extractor.cursor()), arg.value);
    }
  }
  EXPECT_EQ(extractor.Get(base, "custom_dimension.rank"),
            std::numeric_limits<uint32_t>::max());
  ASSERT_NE(extractor.Get(extended, "custom_dimension.rank"),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(GetArgValue(storage, extractor.cursor()), extra.value);
}

TEST(ArgsTrackerTest, ExtendArgSetUpdatePolicies) {
  TraceStorage storage;
  ArgsTracker tracker(&storage);
  StringId key = storage.InternString("key");
  ArgsTracker::CompactArg original{key, key, Variadic::Integer(1)};
  ArgSetId base = tracker.AddArgSet(&original, 1);
  ArgsTracker::CompactArg extra{key, key, Variadic::Integer(2),
                                ArgsTracker::UpdatePolicy::kSkipIfExists};
  EXPECT_EQ(tracker.ExtendArgSet(base, &extra, 1), base);
  extra.update_policy = ArgsTracker::UpdatePolicy::kAddOrUpdate;
  ArgSetId updated = tracker.ExtendArgSet(base, &extra, 1);
  ArgExtractor extractor(storage.arg_table());
  ASSERT_NE(extractor.Get(updated, "key"),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(GetArgValue(storage, extractor.cursor()), extra.value);
  EXPECT_EQ(tracker.ExtendArgSet(std::nullopt, &extra, 1), updated);
}

}  // namespace
}  // namespace perfetto::trace_processor
