#include <cstring>
#include <cmath>
#include "is_buffering.h"

using namespace is;

Reader::~Reader() {}

Writer::~Writer() {}

Reader& operator>>(Reader &b, std::string &s) {
	uint64_t size = 0;
	b >> size;
	s = std::string(size, '\0');
	b.read(&s[0], size);
	return b;
}

Writer& operator<<(Writer &b, const std::string &s) {
	const uint64_t size = s.size();
	b << size;
	b.write(s.c_str(), size);
	return b;
}

ReadBuffer::ReadBuffer(const std::vector<char> &buffer)
	: begin(0), buffer(buffer)
{}
void ReadBuffer::read(char *out, const size_t nbytes) {
	if (begin + nbytes > buffer.size()) {
		throw std::runtime_error("Buffer out of bytes for read");
	}
	std::memcpy(out, &buffer[begin], nbytes);
	begin += nbytes;
}

WriteBuffer::WriteBuffer(const size_t initial_capacity)
	: end(0), buffer(initial_capacity, 0)
{}
void WriteBuffer::write(const char *data, const size_t nbytes) {
	if (end + nbytes > buffer.size()) {
		buffer.resize(std::max(size_t(std::ceil(buffer.size() * 1.5)),
					buffer.size() + nbytes));
	}
	std::memcpy(&buffer[end], data, nbytes);
	end += nbytes;
}
char* WriteBuffer::data() {
	return buffer.data();
}
size_t WriteBuffer::size() const {
	return end;
}

