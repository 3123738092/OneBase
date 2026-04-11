#include "onebase/execution/executors/update_executor.h"
#include "onebase/type/type_id.h"
#include "onebase/type/value.h"

namespace onebase {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void UpdateExecutor::Init() {
  child_executor_->Init();
  has_updated_ = false;
}

auto UpdateExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_updated_) {
    return false;
  }
  has_updated_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  const auto &schema = table_info->schema_;

  int updated = 0;
  Tuple old_tuple;
  RID old_rid;
  while (child_executor_->Next(&old_tuple, &old_rid)) {
    std::vector<Value> new_values;
    new_values.reserve(plan_->GetUpdateExpressions().size());
    for (const auto &expr : plan_->GetUpdateExpressions()) {
      new_values.push_back(expr->Evaluate(&old_tuple, &schema));
    }
    Tuple new_tuple(std::move(new_values));
    if (table_info->table_->UpdateTuple(old_rid, new_tuple)) {
      updated++;
    }
  }

  *tuple = Tuple({Value(TypeId::INTEGER, updated)});
  if (rid != nullptr) {
    *rid = RID();
  }
  return true;
}

}  // namespace onebase
