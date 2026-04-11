#include "onebase/execution/executors/seq_scan_executor.h"
#include "onebase/common/exception.h"

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

    *tuple = cur;
    *rid = cur_rid;
    return true;
  }
  return false;
}

}  // namespace onebase
