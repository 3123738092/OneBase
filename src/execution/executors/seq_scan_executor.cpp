#include "onebase/execution/executors/seq_scan_executor.h"
#include "onebase/type/value.h"

namespace onebase {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (iter_ != end_) {
    Tuple cur = *iter_;
    RID cur_rid = iter_.GetRID();
    ++iter_;

    if (plan_->GetPredicate() != nullptr) {
      auto pred = plan_->GetPredicate()->Evaluate(&cur, &table_info_->schema_);
      if (!pred.GetAsBoolean()) {
        continue;
      }
    }

    std::vector<Value> vals;
    vals.reserve(table_info_->schema_.GetColumnCount());
    for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); ++i) {
      vals.push_back(cur.GetValue(&table_info_->schema_, i));
    }
    *tuple = Tuple(std::move(vals));
    *rid = cur_rid;
    return true;
  }
  return false;
}

}  // namespace onebase
