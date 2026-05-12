#include "onebase/execution/executors/delete_executor.h"
#include "onebase/catalog/catalog.h"
#include "onebase/type/type_id.h"
#include "onebase/type/value.h"

namespace onebase {

namespace {

auto RemoveIndexEntries(Catalog *catalog, TableInfo *table_info, const Tuple &tuple, const RID &rid) -> void {
  for (auto *index_info : catalog->GetTableIndexes(table_info->name_)) {
    if (!index_info->SupportsPointLookup()) {
      continue;
    }
    const auto key =
        tuple.GetValue(&table_info->schema_, index_info->GetLookupAttr()).GetAsInteger();
    index_info->RemoveEntry(key, rid);
  }
}

}  // namespace

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  child_executor_->Init();
  has_deleted_ = false;
}

auto DeleteExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_deleted_) {
    return false;
  }
  has_deleted_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());

  int deleted = 0;
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    const Tuple stored_tuple = table_info->table_->GetTuple(child_rid);
    RemoveIndexEntries(catalog, table_info, stored_tuple, child_rid);
    table_info->table_->DeleteTuple(child_rid);
    deleted++;
  }

  *tuple = Tuple({Value(TypeId::INTEGER, deleted)});
  if (rid != nullptr) {
    *rid = RID();
  }
  return true;
}

}  // namespace onebase
