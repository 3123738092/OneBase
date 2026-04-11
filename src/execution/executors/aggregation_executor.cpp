#include "onebase/execution/executors/aggregation_executor.h"
#include <unordered_map>
#include "onebase/type/type_id.h"
#include "onebase/type/value.h"

namespace onebase {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                          std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void AggregationExecutor::Init() {
  child_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;

  using AggVals = std::vector<Value>;
  std::unordered_map<std::string, std::pair<std::vector<Value>, AggVals>> groups;

  auto init_aggs = [this, &groups](const std::string &key, const std::vector<Value> &group_vals) {
    AggVals vals;
    vals.reserve(plan_->GetAggregateTypes().size());
    for (auto agg_type : plan_->GetAggregateTypes()) {
      switch (agg_type) {
        case AggregationType::CountStarAggregate:
        case AggregationType::CountAggregate:
          vals.emplace_back(TypeId::INTEGER, 0);
          break;
        case AggregationType::SumAggregate:
        case AggregationType::MinAggregate:
        case AggregationType::MaxAggregate:
          vals.emplace_back(TypeId::INTEGER);
          break;
      }
    }
    groups.emplace(key, std::make_pair(group_vals, vals));
  };

  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    std::vector<Value> group_vals;
    group_vals.reserve(plan_->GetGroupBys().size());
    std::string group_key;
    for (const auto &expr : plan_->GetGroupBys()) {
      auto v = expr->Evaluate(&child_tuple, &child_executor_->GetOutputSchema());
      group_vals.push_back(v);
      group_key += v.ToString();
      group_key.push_back('|');
    }

    if (groups.find(group_key) == groups.end()) {
      init_aggs(group_key, group_vals);
    }

    auto &agg_vals = groups[group_key].second;
    for (size_t i = 0; i < plan_->GetAggregateTypes().size(); i++) {
      auto agg_type = plan_->GetAggregateTypes()[i];
      Value input = plan_->GetAggregates()[i]->Evaluate(&child_tuple, &child_executor_->GetOutputSchema());
      switch (agg_type) {
        case AggregationType::CountStarAggregate:
          agg_vals[i] = Value(TypeId::INTEGER, agg_vals[i].GetAsInteger() + 1);
          break;
        case AggregationType::CountAggregate:
          if (!input.IsNull()) {
            agg_vals[i] = Value(TypeId::INTEGER, agg_vals[i].GetAsInteger() + 1);
          }
          break;
        case AggregationType::SumAggregate:
          if (agg_vals[i].IsNull()) {
            agg_vals[i] = input;
          } else {
            agg_vals[i] = agg_vals[i].Add(input);
          }
          break;
        case AggregationType::MinAggregate:
          if (agg_vals[i].IsNull() || input.CompareLessThan(agg_vals[i]).GetAsBoolean()) {
            agg_vals[i] = input;
          }
          break;
        case AggregationType::MaxAggregate:
          if (agg_vals[i].IsNull() || input.CompareGreaterThan(agg_vals[i]).GetAsBoolean()) {
            agg_vals[i] = input;
          }
          break;
      }
    }
  }

  if (groups.empty() && plan_->GetGroupBys().empty()) {
    std::vector<Value> out_vals;
    for (auto agg_type : plan_->GetAggregateTypes()) {
      switch (agg_type) {
        case AggregationType::CountStarAggregate:
        case AggregationType::CountAggregate:
          out_vals.emplace_back(TypeId::INTEGER, 0);
          break;
        case AggregationType::SumAggregate:
        case AggregationType::MinAggregate:
        case AggregationType::MaxAggregate:
          out_vals.emplace_back(TypeId::INTEGER);
          break;
      }
    }
    result_tuples_.emplace_back(std::move(out_vals));
    return;
  }

  for (auto &entry : groups) {
    std::vector<Value> out_vals = entry.second.first;
    const auto &agg_vals = entry.second.second;
    out_vals.insert(out_vals.end(), agg_vals.begin(), agg_vals.end());
    result_tuples_.emplace_back(std::move(out_vals));
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  if (rid != nullptr) {
    *rid = RID();
  }
  return true;
}

}  // namespace onebase
