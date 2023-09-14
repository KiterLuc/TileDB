/**
 * @file buffered_chunk.h
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
 * This file declares the BufferedChunk class.
 */

#ifndef TILEDB_FILESYSTEM_BUFFERED_CHUNK_H
#define TILEDB_FILESYSTEM_BUFFERED_CHUNK_H

namespace tiledb::sm::filesystem {

struct BufferedChunk {
  std::string uri;
  uint64_t size;

  BufferedChunk()
      : uri("")
      , size(0) {
  }
  BufferedChunk(std::string chunk_uri, uint64_t chunk_size)
      : uri(chunk_uri)
      , size(chunk_size) {
  }
};

#endif // TILEDB_FILESYSTEM_BUFFERED_CHUNK_H