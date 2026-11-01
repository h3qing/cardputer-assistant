#include "outbox_migrate.h"

#include "outbox_io.h"
#include "protocol_limits.h"
#include "store_commit.h"

namespace cpa {
namespace {

bool migrate_one(FileStore& from, FileStore& to, const MonoId& id) {
  // Static, not on the 16 KB loop stack (05 §5.13): one outbox line plus one byte to
  // tell a larger file, which is never truncated into the target.
  static char buf[limits::kLineMax + 1];
  const OutboxPath json = outbox_path(id, OutboxFile::Json);
  const long n = from.read(json.c_str(), buf, sizeof buf);
  if (n < 0 || static_cast<size_t>(n) > limits::kLineMax) return false;
  const OutboxPath tmp = outbox_path(id, OutboxFile::Tmp);
  return commit_file(to, json.c_str(), tmp.c_str(), buf, static_cast<size_t>(n)) && from.remove(json.c_str());
}

}  // namespace

MigrateResult outbox_migrate(FileStore& from, FileStore& to, const OutboxCore& pending, MigrateObserver& obs) {
  MigrateResult r;
  if (pending.n > 0) to.mkdir(kOutboxDir);
  for (uint16_t i = 0; i < pending.n; ++i) {
    if (migrate_one(from, to, pending.ids[i])) {
      r.moved++;
    } else {
      r.failed++;
    }
    obs.on_item(static_cast<uint16_t>(i + 1), pending.n);
  }
  return r;
}

}  // namespace cpa
