// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab

#include "ScrubStore.h"
#include "osd_types.h"
#include "common/scrub_types.h"
#include "include/rados/rados_types.hpp"

namespace {
hobject_t make_scrub_object(const spg_t& pgid)
{
  ostringstream ss;
  ss << "scrub_" << pgid;
  return pgid.make_temp_object(ss.str());
}

string first_object_key(int64_t pool)
{
  return "SCRUB_OBJ_" + std::to_string(pool) + "-";
}

// the object_key should be unique across pools
string to_object_key(int64_t pool, const librados::object_id_t& oid)
{
  return ("SCRUB_OBJ_" +
	  std::to_string(pool) + "." +
	  oid.name + oid.nspace + std::to_string(oid.snap));
}

string last_object_key(int64_t pool)
{
  return "SCRUB_OBJ_" + std::to_string(pool) + "/";
}

string first_snap_key(int64_t pool)
{
  return "SCRUB_SS_" + std::to_string(pool) + "-";
}

string to_snap_key(int64_t pool, const librados::object_id_t& oid)
{
  return "SCRUB_SS_" + std::to_string(pool) + "." + oid.name + oid.nspace;
}

string last_snap_key(int64_t pool)
{
  return "SCRUB_SS_" + std::to_string(pool) + "/";
}
}

namespace Scrub {

Store*
Store::create(ObjectStore* store,
	      ObjectStore::Transaction* t,
	      const spg_t& pgid,
	      const coll_t& coll)
{
  assert(store);
  assert(t);
  hobject_t oid = make_scrub_object(pgid);
  coll_t temp_coll = coll.get_temp();
  t->touch(temp_coll, ghobject_t{oid});
  return new Store{temp_coll, oid, store};
}

Store::Store(const coll_t& coll, const hobject_t& oid, ObjectStore* store)
  : coll(coll),
    hoid(oid),
    driver(store, coll, hoid),
    backend(&driver)
{}

Store::~Store()
{
  assert(results.empty());
}

void Store::add_object_error(int64_t pool, const inconsistent_obj_wrapper& e)
{
  bufferlist bl;
  e.encode(bl);
  results[to_object_key(pool, e.object)] = bl;
}

void Store::add_snap_error(int64_t pool, const inconsistent_snapset_wrapper& e)
{
  bufferlist bl;
  e.encode(bl);
  results[to_snap_key(pool, e.object)] = bl;
}

bool Store::empty() const
{
  return results.empty();
}

void Store::flush(ObjectStore::Transaction* t)
{
  OSDriver::OSTransaction txn = driver.get_transaction(t);
  backend.set_keys(results, &txn);
  results.clear();
}

void Store::cleanup(ObjectStore::Transaction* t)
{
  assert(t);
  OSDriver::OSTransaction txn = driver.get_transaction(t);
  backend.clear(&txn);
  t->remove(coll, hoid);
}

std::vector<bufferlist>
Store::get_snap_errors(ObjectStore* store,
		       int64_t pool,
		       const librados::object_id_t& start,
		       uint64_t max_return)
{
  const string begin = (start.name.empty() ?
			first_snap_key(pool) : to_snap_key(pool, start));
  const string end = last_snap_key(pool);
  return get_errors(store, begin, end, max_return);     
}

std::vector<bufferlist>
Store::get_object_errors(ObjectStore* store,
			 int64_t pool,
			 const librados::object_id_t& start,
			 uint64_t max_return)
{
  const string begin = (start.name.empty() ?
			first_object_key(pool) : to_object_key(pool, start));
  const string end = last_object_key(pool);
  return get_errors(store, begin, end, max_return);
}

std::vector<bufferlist>
Store::get_errors(ObjectStore* store,
		  const string& begin,
		  const string& end,
		  uint64_t max_return)
{
  vector<bufferlist> errors;
  auto next = std::make_pair(begin, bufferlist{});
  while (max_return && !backend.get_next(next.first, &next)) {
    if (next.first >= end)
      break;
    errors.push_back(next.second);
    max_return--;
  }
  return errors;
}
string to_snap_key(int64_t pool, const librados::object_id_t& oid)
{
  return "SCRUB_SS_" + std::to_string(pool) + "." + oid.name + oid.nspace;
}


} // namespace Scrub
