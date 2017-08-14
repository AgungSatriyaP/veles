/*
 * Copyright 2017 CodiLime
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "util/edit.h"

namespace veles {
namespace util {

void EditEngine::changeBytes(size_t pos, const data::BinData& bytes,
                             bool add_to_history) {
  if (bytes.size() == 0) {
    return;
  }
  assert(pos + bytes.size() <= original_data_->binData().size());

  if (add_to_history) {
    edit_stack_data_.push_back(bytesValues(pos, bytes.size()));
    edit_stack_.push_back(QPair<size_t, size_t>(pos, bytes.size()));
    while (edit_stack_.size() > edit_stack_limit_) {
      edit_stack_data_.pop_front();
      edit_stack_.pop_front();
    }
  }

  has_changes_ = true;

  size_t end_pos = pos + bytes.size();
  auto next_it = address_mapping_.upperBound(pos);
  assert(next_it != address_mapping_.cbegin());
  auto it = next_it;
  --it;

  if (next_it == address_mapping_.cend() || end_pos <= next_it.key()) {
    if (it->fragment_ == nullptr) {
      if (address_mapping_.find(end_pos) == address_mapping_.cend()) {
        address_mapping_.insert(
            end_pos, EditNode(nullptr, it->offset_ + (end_pos - it.key())));
      }
      address_mapping_.insert(pos, EditNode(bytes));
      // this node can overwrite `*it` and that's OK
    } else {
      it->fragment_->setData(it->offset_ + (pos - it.key()), bytes.size(),
                             bytes);
    }
  } else {
    it = next_it;
    ++next_it;
    while (next_it != address_mapping_.cend() && next_it.key() < end_pos) {
      address_mapping_.erase(it);
      it = next_it;
      ++next_it;
    }

    EditNode last_modified_node = it.value();
    last_modified_node.offset_ += end_pos - it.key();
    address_mapping_.erase(it);

    if (address_mapping_.find(end_pos) == address_mapping_.cend()) {
      address_mapping_.insert(end_pos, last_modified_node);
    }
    address_mapping_.insert(pos, EditNode(bytes));
    // it can overwrite the first overlapping node and that's OK
  }
}

size_t EditEngine::undo() {
  if (!hasUndo()) {
    return 0;
  }

  auto range = edit_stack_.back();
  auto last_change = edit_stack_data_.back();

  changeBytes(range.first, last_change, false);

  edit_stack_data_.pop_back();
  edit_stack_.pop_back();

  return range.first;
}

void EditEngine::applyChanges() {
  for (auto it = address_mapping_.cbegin(); it != address_mapping_.cend();
       ++it) {
    if (it->fragment_ != nullptr) {
      auto next_it = it;
      ++next_it;
      assert(next_it != address_mapping_.cend());

      size_t size = next_it.key() - it.key();
      assert(it->offset_ + size <= it->fragment_->size());

      original_data_->uploadNewData(it->fragment_->data(it->offset_, size),
                                    it.key());
    }
  }
  initAddressMapping();

  for (auto it = changes_.cbegin(); it != changes_.cend(); ++it) {
    original_data_->uploadNewData(it.value(), it.key());
  }
  changes_.clear();
}

void EditEngine::clear() {
  initAddressMapping();
  changes_.clear();
  edit_stack_data_.clear();
  edit_stack_.clear();
}

uint64_t EditEngine::byteValue(size_t pos) const {
  auto next_it = address_mapping_.upperBound(pos);
  assert(next_it != address_mapping_.cbegin());
  auto it = next_it;
  --it;

  if (it->fragment_ == nullptr) {
    return original_data_->binData().element64(it->offset_ + (pos - it.key()));
  } else {
    return it->fragment_->element64(it->offset_ + (pos - it.key()));
  }
}

uint64_t EditEngine::originalByteValue(size_t pos) const {
  return original_data_->binData().element64(pos);
}

data::BinData EditEngine::bytesValues(size_t pos, size_t size) const {
  data::BinData result = data::BinData(original_data_->binData().width(), size);

  size_t end_pos = pos + size;
  auto next_it = address_mapping_.upperBound(pos);
  assert(next_it != address_mapping_.cbegin());
  auto it = next_it;
  --it;

  if (next_it == address_mapping_.cend() || end_pos <= next_it.key()) {
    result.setData(0, size,
                   getDataFromEditNode(it.value(), pos - it.key(), size));
  } else {
    size_t size_to_write = next_it.key() - pos;
    result.setData(
        0, size_to_write,
        getDataFromEditNode(it.value(), pos - it.key(), size_to_write));
    size_t bytes_written = size_to_write;

    it = next_it;
    ++next_it;
    while (next_it != address_mapping_.cend() && next_it.key() < end_pos) {
      size_to_write = next_it.key() - it.key();
      result.setData(bytes_written, size_to_write,
                     getDataFromEditNode(it.value(), 0, size_to_write));
      bytes_written += size_to_write;
      it = next_it;
      ++next_it;
    }

    size_to_write = size - bytes_written;
    result.setData(bytes_written, size_to_write,
                   getDataFromEditNode(it.value(), 0, size_to_write));
  }

  return result;
}

data::BinData EditEngine::originalBytesValues(size_t pos, size_t size) const {
  return original_data_->binData().data(pos, size);
}

data::BinData EditEngine::getDataFromEditNode(const EditNode& edit_node,
                                              size_t offset,
                                              size_t size) const {
  if (edit_node.fragment_ == nullptr) {
    return original_data_->binData().data(edit_node.offset_ + offset, size);
  } else {
    return edit_node.fragment_->data(edit_node.offset_ + offset, size);
  }
}

void EditEngine::initAddressMapping() {
  address_mapping_.clear();
  address_mapping_.insert(0, EditNode(nullptr, 0));
  has_changes_ = false;
}

}  // namespace util
}  // namespace veles
