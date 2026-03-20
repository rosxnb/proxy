#pragma once

#include <vector>
#include <span>
#include <string_view>

/**
 * @class Buffer
 * @brief A contiguous, appendable byte buffer with a sliding read cursor.
 *
 * Design intent:
 * - Append raw bytes from socket reads on the "write" end.
 * - Consume bytes from the "read" end after forwarding them.
 * - compact() is called lazily to avoid frequent memcpy.
 */
class Buffer
{
public:
    /// Default capacity allocated for new buffers (64 KB).
    static constexpr size_t kDefaultCapacity = 64 * 1024;

    /**
     * @brief Constructs a new Buffer.
     * @param capacity The initial capacity to reserve (defaults to kDefaultCapacity).
     */
    explicit constexpr Buffer(size_t capacity = kDefaultCapacity)
    { data_.reserve(capacity); }

    /**
     * @brief Appends raw bytes to the end of the buffer.
     * @param src Pointer to the start of the data to append.
     * @param len Number of bytes to append.
     */
    constexpr void append(std::span<uint8_t const> data)
    { data_.append_range(data); }

    /**
     * @brief Appends all unconsumed bytes from another Buffer.
     * @param other The Buffer to copy data from.
     */
    constexpr void append(Buffer const& other)
    { append(std::span{other.data(), other.size()}); }

    /**
     * @brief Gets a read-only pointer to the first unconsumed byte.
     * @return A const pointer to the current read position.
     */
    [[nodiscard]] constexpr uint8_t const* data() const
    { return data_.data() + rpos_; }

    /**
     * @brief Gets a mutable pointer to the first unconsumed byte.
     * @return A pointer to the current read position.
     */
    [[nodiscard]] constexpr uint8_t* data()
    { return data_.data() + rpos_; }

    /**
     * @brief Calculates the number of unconsumed bytes available to read.
     * @return The number of readable bytes.
     */
    [[nodiscard]] constexpr size_t size() const
    { return data_.size() - rpos_; }

    /**
     * @brief Checks if there are no unconsumed bytes left.
     * @return true if the buffer is logically empty, false otherwise.
     */
    [[nodiscard]] constexpr bool empty() const
    { return size() == 0; }

    /**
     * @brief Advances the read cursor by a specified number of bytes.
     * * @note This function marks bytes as consumed. If the buffer is completely
     * drained, it resets cheaply. If the read cursor advances past half the 
     * default capacity, it lazily compacts the buffer to free up memory.
     * * @param n The number of bytes to consume. It is safely clamped to readable().
     */
    constexpr void advance(size_t n)
    {
        n = std::min(n, size());
        rpos_ += n;

        if (rpos_ == data_.size()) {
            clear();
        }

        // Slide data to front if wasted space is more than half
        else if (rpos_ > data_.size() / 2) {
            compact();         
        }
    }

    /**
     * @brief Fully clears the buffer and resets the read cursor.
     */
    constexpr void clear()
    {
        data_.clear();
        rpos_ = 0;
    }

    /**
     * @brief Moves unread bytes to the front of the underlying vector.
     * * This reclaims memory at the front of the vector by erasing consumed bytes.
     * It is automatically called by consume() when a threshold is reached, 
     * but can be invoked manually if necessary.
     */
    constexpr void compact()
    {
        if (rpos_ == 0)
            return;
        data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(rpos_));
        rpos_ = 0;
    }

    /**
     * @brief Creates a string_view representing the unconsumed portion of the buffer.
     * * @warning The returned string_view is non-owning. It is only valid as long 
     * as the Buffer is not modified. Operations like append(), consume(), or 
     * compact() may reallocate or shift the underlying memory, invalidating this view.
     * * @return A std::string_view of the readable bytes.
     */
    [[nodiscard]] constexpr std::string_view as_view() const
    { return {reinterpret_cast<char const*>(data()), size()}; }

private:
    std::vector<uint8_t> data_; ///< Underlying contiguous storage.
    size_t               rpos_ = 0; ///< The current read cursor position.
};
