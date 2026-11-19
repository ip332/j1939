//
// Created by igor on 9/4/21.
//

#include "j1939_frame.h"

#include <algorithm>
#include <cmath>

uint64_t J1939_frame::read_bits(uint16_t start_bit, uint16_t length, bool little_endian) const {
    // An integer signal is at most 64 bits. Length 0 would underflow the end-bit math.
    if (length == 0 || length > 64) {
        return 0;
    }
    uint64_t target = 0;
    // Motorola (big endian)
    if (!little_endian) {
        uint8_t msb_index = start_bit / 8;
        uint8_t msb_len = fmin((start_bit% 8) + 1,length);

        if(msb_len >= length) {
            // Extract LSB
            target |= buffer_[msb_index] >> ((start_bit%8) - length + 1);
        }else{
            uint8_t lsb_index = msb_index + ceil((length - msb_len)/8.0);
            uint8_t lsb_len = (length-msb_len-1) %8 + 1;
            uint8_t lsb_bit_offset = 8-lsb_len;

            // Extract LSB
            target |= (buffer_[lsb_index] >> lsb_bit_offset) & make_mask(lsb_len);
            // Extract remaining bytes
            uint8_t loaded_bytes = 1;
            for(uint8_t index = msb_index; index < lsb_index; index++){
                int8_t shift = length - msb_len - ((loaded_bytes - 1) * 8);
                // Cast before the shift. The byte otherwise promotes to a 32-bit int,
                // and a 32..63 bit shift is undefined.
                if (index == msb_index) {
                    target |= (static_cast<uint64_t>(buffer_[index]) & make_mask(msb_len)) << shift;
                } else {
                    target |= static_cast<uint64_t>(buffer_[index]) << shift;
                }
                loaded_bytes++;
            }
        }
    } else {
        uint8_t lsb_index = start_bit/8;
        uint8_t lsb_bit_offset = start_bit%8;
        uint8_t msb_index = (start_bit+ length - 1) / 8;

        // Extract bits of LSB
        target |= (buffer_[lsb_index] >> lsb_bit_offset);

        // Extract the rest of the payload
        uint8_t loaded_bytes = 1;
        for (int16_t index = lsb_index+1; index <= msb_index; index++){
            uint8_t shift = (loaded_bytes * 8) - lsb_bit_offset;
            target |= static_cast<uint64_t>(buffer_[index]) << shift; // MUST cast the byte to uint64_t otherwise sign extension is performed
            loaded_bytes++;
        }
    }

    // Mask value
    target &= make_mask(length);
    return target;
}

void J1939_frame::write_bits(uint64_t value, uint16_t start_bit, uint16_t length, bool little_endian) {
    // Same refusal as read_bits: do not walk off the buffer for an empty or byte-string field.
    if (length == 0 || length > 64) {
        return;
    }
    // Mask the value
    value &= make_mask(length);

    if (!little_endian) {
        uint8_t msb_index = start_bit / 8;
        uint8_t msb_len = fmin((start_bit % 8) +1,length);
        uint8_t lsb_index = msb_index + ceil((length - msb_len)/8.0);
        uint8_t lsb_len = (length-msb_len-1)%8+1;
        uint8_t lsb_bit_offset = 8-lsb_len;

        // Signal spans a signal byte
        if(msb_index == lsb_index){
            // Create a mask of ones the length of the signal and shift to proper location
            uint8_t shift = (start_bit%8) + 1 - length;
            uint8_t mask = ((1u << length) - 1) << shift;
            // clear existing data
            buffer_[msb_index] &= ~mask;
            // Load in signal
            buffer_[msb_index] |= (value << shift) & mask;
        } else {
            // Load LSB first
            uint8_t lsb_mask = 0xFF << lsb_bit_offset; // Generate mask for active bits in this byte
            buffer_[lsb_index] &= ~lsb_mask; // Clear any existing data from this region
            buffer_[lsb_index] |= (value << lsb_bit_offset) & lsb_mask;
            // Load the remaining bits (LSB+1 to MSB)
            uint8_t loaded_bytes = 1;
            for(int8_t index = lsb_index - 1; index >= msb_index; index--){
                // Create mask for each byte in the frame
                uint8_t mask = 0xFF; // single byte
                // Check if this is our last byte and only have to apply to part of the byte
                int8_t remaining_bits = length - ((loaded_bytes + 1) * 8) + lsb_bit_offset;
                if(remaining_bits < 0){
                    mask = 0xFF >> (remaining_bits * -1);
                }
                // Clear any existing data from this region(byte)
                buffer_[index] &= ~mask;
                // Load shifted signal into proper byte
                uint8_t right_shift = 8 * (loaded_bytes-1) + lsb_len;
                buffer_[index] |= (value >> right_shift) & mask;
                loaded_bytes++;
            }
        }
    } else {
        uint8_t lsb_index = start_bit/8;
        uint8_t lsb_bit_offset = start_bit %8;
        uint8_t mask = ((0x01<<static_cast<uint8_t>(fmin(8-lsb_bit_offset,length)))-1) << lsb_bit_offset;
        // Clear data in LSB byte
        buffer_[lsb_index] &= ~mask;
        // Write bits of startbyte
        buffer_[lsb_index] |= (value << lsb_bit_offset) & mask;
        uint8_t msb_index = (start_bit + length - 1) / 8;
        uint8_t loaded_bytes = 1;
        for (int16_t index = lsb_index + 1; index <= msb_index; index++) {
            int8_t remaining_bits = length - (8-lsb_bit_offset) - loaded_bytes*8;
            // Create mask for signal
            if(remaining_bits < 0) {
                mask = 0xFF >> (remaining_bits * -1);
            } else {
                mask = 0xFF;
            }
            // Clear data in signal
            buffer_[index] &= ~mask;

            // Shift in signal
            uint8_t right_shift = loaded_bytes * 8 - lsb_bit_offset;//(loaded_bytes-1)*8 + (8 - LSB_bit_offset);
            buffer_[index] |= (value >> right_shift) & mask;
            loaded_bytes++;
        }
    }
}

std::string SPN::toString(uint64_t value) const {
    std::string name(name_);
    if (!enum_names_.empty()) {
        if (enum_names_.find(value) == enum_names_.end()) {
            return " " + name + ": \"??\"";
        }
        return " " + name + ": " + enum_names_.at(value);
    }
    // The sign bit lives in a uint64. A byte string has no integer value here.
    if (length_ == 0 || length_ > 64) {
        return " " + name + ": 0";
    }
    std::string value_str;
    if (signedness_) {
        int64_t msb_sign_mask = 1ULL << (length_ - 1);
        value = (static_cast<int64_t>(value) ^ msb_sign_mask) - msb_sign_mask;
        auto result = static_cast<int64_t>(value);
        if ((scalar_ != 1) || (offset_ != 0)) {
            return " " + name + ": " + std::to_string(result * scalar_ + offset_);
        }
        return " " + name + ": " + std::to_string(result);
    }
    if ((scalar_ != 1) || (offset_ != 0)) {
        return " " + name + ": " + std::to_string(static_cast<uint64_t>((value & make_mask(length_)) * scalar_ + offset_));
    }
    return " " + name + ": " + std::to_string(value);
}

int64_t SPN::toInteger(uint64_t value) const {
    if (length_ == 0 || length_ > 64) {
        return 0;
    }
    value &= make_mask(length_);
    // perform sign extension
    if (signedness_){
        int64_t msb_sign_mask = 1ULL << (length_ - 1);
        value = ((int64_t)value ^ msb_sign_mask) - msb_sign_mask;
        int64_t result = static_cast<int64_t>(value);
        return result * scalar_ + offset_;
    }
    return value * scalar_ + offset_;
}

double SPN::toDouble(uint64_t value) const {
    if (length_ == 0 || length_ > 64) {
        return 0;
    }
    value &= make_mask(length_);
    // perform sign extension
    if (signedness_){
        int64_t msb_sign_mask = 1ULL << (length_ - 1);
        value = ((int64_t)value ^ msb_sign_mask) - msb_sign_mask;
        int64_t result = static_cast<int64_t>(value);
        if ((scalar_ == 1) && (offset_ == 0)) {
            return result;
        }
        return result * scalar_ + offset_;
    }
    if ((scalar_ == 1) && (offset_ == 0)) {
        return value;
    }
    return value * scalar_ + offset_;
}

const SPN * PGN::signalByName(const char * name) const {
    for (const auto & it : signals_) {
        if (!strcmp(name, it.name_.c_str())) {
            return &it;
        }
    }
    return nullptr;
}

