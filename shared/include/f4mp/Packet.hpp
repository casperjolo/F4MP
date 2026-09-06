#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Minimal little-endian byte writer / reader used for every wire message.
// Strings are encoded as u16 length + UTF-8 bytes (max 4096 bytes).
namespace f4mp
{
	inline constexpr std::size_t MAX_STRING_BYTES = 4096;

	class Writer
	{
	public:
		Writer() { _buf.reserve(128); }

		void U8(std::uint8_t v) { _buf.push_back(v); }
		void U16(std::uint16_t v) { Raw(&v, sizeof(v)); }
		void U32(std::uint32_t v) { Raw(&v, sizeof(v)); }
		void I32(std::int32_t v) { Raw(&v, sizeof(v)); }
		void F32(float v) { Raw(&v, sizeof(v)); }
		void Bool(bool v) { U8(v ? 1 : 0); }

		void Str(std::string_view s)
		{
			const auto len = s.size() > MAX_STRING_BYTES ? MAX_STRING_BYTES : s.size();
			U16(static_cast<std::uint16_t>(len));
			Raw(s.data(), len);
		}

		[[nodiscard]] const std::vector<std::uint8_t>& Data() const noexcept { return _buf; }
		[[nodiscard]] std::vector<std::uint8_t> Take() noexcept { return std::move(_buf); }

	private:
		void Raw(const void* p, std::size_t n)
		{
			static_assert(std::endian::native == std::endian::little, "F4MP assumes a little-endian host");
			const auto* bytes = static_cast<const std::uint8_t*>(p);
			_buf.insert(_buf.end(), bytes, bytes + n);
		}

		std::vector<std::uint8_t> _buf;
	};

	class Reader
	{
	public:
		Reader(const std::uint8_t* data, std::size_t size) noexcept
			: _data(data), _size(size)
		{
		}

		explicit Reader(std::span<const std::uint8_t> bytes) noexcept
			: Reader(bytes.data(), bytes.size())
		{
		}

		[[nodiscard]] bool Ok() const noexcept { return _ok; }
		[[nodiscard]] std::size_t Remaining() const noexcept { return _size - _pos; }

		std::uint8_t U8() { std::uint8_t v{}; Raw(&v, sizeof(v)); return v; }
		std::uint16_t U16() { std::uint16_t v{}; Raw(&v, sizeof(v)); return v; }
		std::uint32_t U32() { std::uint32_t v{}; Raw(&v, sizeof(v)); return v; }
		std::int32_t I32() { std::int32_t v{}; Raw(&v, sizeof(v)); return v; }
		float F32() { float v{}; Raw(&v, sizeof(v)); return v; }
		bool Bool() { return U8() != 0; }

		std::string Str()
		{
			const auto len = U16();
			if (!_ok || len > MAX_STRING_BYTES || len > Remaining()) {
				_ok = false;
				return {};
			}
			std::string s(reinterpret_cast<const char*>(_data + _pos), len);
			_pos += len;
			return s;
		}

	private:
		void Raw(void* out, std::size_t n)
		{
			if (!_ok || n > Remaining()) {
				_ok = false;
				std::memset(out, 0, n);
				return;
			}
			std::memcpy(out, _data + _pos, n);
			_pos += n;
		}

		const std::uint8_t* _data;
		std::size_t _size;
		std::size_t _pos{ 0 };
		bool _ok{ true };
	};
}
