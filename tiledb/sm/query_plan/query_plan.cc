/**
 * @file   query_plan.cc
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2023 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 This file implements class QueryPlan.
 */

#include "tiledb/sm/query_plan/query_plan.h"

#include "tiledb/sm/array/array.h"
#include "tiledb/sm/enums/array_type.h"
#include "tiledb/sm/enums/layout.h"
#include "tiledb/sm/enums/query_status.h"
#include "tiledb/sm/filesystem/uri.h"
#include "tiledb/sm/query/query.h"
#include "tiledb/sm/rest/rest_client.h"

using namespace tiledb::common;

namespace tiledb {
namespace sm {

/* ********************************* */
/*     CONSTRUCTORS & DESTRUCTORS    */
/* ********************************* */
QueryPlan::QueryPlan(Query& query) {
  if (query.array()->is_remote()) {
    auto rest_client = query.rest_client();
    if (!rest_client) {
      throw std::runtime_error(
          "Failed to create a query plan; Remote query"
          "with no REST client.");
    }

    query_plan_ = rest_client->post_query_plan_from_rest(
        query.array()->array_uri(), query);
    // We need to transition the query status to INITIALIZED to mimic the
    // behavior of getting a query plan locally
    query.set_status(QueryStatus::INITIALIZED);
    return;
  }

  auto array_uri = query.array()->array_uri().to_string();
  // This most probably ends up creating the strategy on the query
  auto strategy_ptr = query.strategy();

  std::vector<std::string> attributes;
  std::vector<std::string> dimensions;
  for (auto& buf : query.buffer_names()) {
    if (query.array()->array_schema_latest().is_dim(buf)) {
      dimensions.push_back(buf);
    } else {
      attributes.push_back(buf);
    }
  }
  if (query.is_dense()) {
    dimensions = query.array()->array_schema_latest().dim_names();
  }
  std::sort(attributes.begin(), attributes.end());
  std::sort(dimensions.begin(), dimensions.end());

  query_plan_ = {
      {"TileDB Query Plan",
       {{"Array.URI", array_uri},
        {"Array.Type",
         array_type_str(query.array()->array_schema_latest().array_type())},
        {"VFS.Backend", URI(array_uri).backend_name()},
        {"Query.Layout", layout_str(query.layout())},
        {"Query.Strategy.Name", strategy_ptr->name()},
        {"Query.Attributes", attributes},
        {"Query.Dimensions", dimensions}}}};
}

}  // namespace sm
}  // namespace tiledb
