#include "serial/StreamBuffer.h"

#include "serial/Serialization.h"

StreamBuffer::StreamBuffer(size_t numBytes)
	: gpos_(0)
	, ppos_(0) {
	buffer_.reserve(numBytes);
}

StreamBuffer::StreamBuffer(const uint8_t* data, size_t numBytes)
	: gpos_(0)
	, ppos_(0) {
	buffer_.reserve(numBytes);
	buffer_.assign(data, data + numBytes);
}

const std::vector<uint8_t>& StreamBuffer::buffer() const {
	return buffer_;
}

void StreamBuffer::seekg(size_t pos) {
	gpos_ = pos;
}

void StreamBuffer::seekp(size_t pos) {
	ppos_ = pos;
}

size_t StreamBuffer::tellg() const {
	return gpos_;
}

size_t StreamBuffer::tellp() const {
	return ppos_;
}

bool StreamBuffer::eof() const {
	return gpos_ >= buffer_.size();
}

StreamBuffer& StreamBuffer::operator<< (uint8_t data) {
	buffer_.push_back(data);
	ppos_++;
	return *this;
}

StreamBuffer& StreamBuffer::operator<< (uint32_t data) {
	std::vector<uint8_t> buff(sizeof(data), 0);
	memcpy(&buff[0], &data, sizeof(data));
	buffer_.insert(buffer_.end(), buff.begin(), buff.end());
	ppos_ += sizeof(data);
	return *this;
}

StreamBuffer& StreamBuffer::operator<< (float32_t data) {
	*this << pack754_32(data);
	return *this;
}

StreamBuffer& StreamBuffer::operator<< (const glm::vec3& data) {
	*this << pack754_32(data.x)
		<< pack754_32(data.y)
		<< pack754_32(data.z);
	return *this;
}

StreamBuffer& StreamBuffer::operator<< (const glm::quat& data) {
	*this << pack754_32(data.x)
		<< pack754_32(data.y)
		<< pack754_32(data.z)
		<< pack754_32(data.w);
	return *this;
}

StreamBuffer& StreamBuffer::operator<< (const Transform::Shared& data) {
	*this << data->translation()
		<< data->rotation()
		<< data->scale();
	return *this;
}

StreamBuffer& StreamBuffer::operator<< (const Command& data) {
	*this << uint8_t(data.type) << data.direction;
	return *this;
}

StreamBuffer& StreamBuffer::operator>> (uint8_t& data) {
	std::memcpy(&data, &buffer_[gpos_], sizeof(data));
	gpos_++;
	return *this;
}

StreamBuffer& StreamBuffer::operator>> (uint32_t& data) {
	std::memcpy(&data, &buffer_[gpos_], sizeof(data));
	gpos_ += sizeof(data);
	return *this;
}

StreamBuffer& StreamBuffer::operator>> (float32_t& data) {
	uint32_t packed;
	*this >> packed;
	std::memcpy(&data, &packed, sizeof(packed));
	return *this;
}

StreamBuffer& StreamBuffer::operator>> (glm::vec3& data) {
	*this >> data.x
		>> data.y
		>> data.z;
	return *this;
}

StreamBuffer& StreamBuffer::operator>> (glm::quat& data) {
	*this >> data.x
		>> data.y
		>> data.z
		>> data.w;
	return *this;
}

StreamBuffer& StreamBuffer::operator>> (Transform::Shared& data) {
	glm::vec3 translation;
	glm::quat rotation;
	glm::vec3 scale;
	*this >> translation
		>> rotation
		>> scale;
	data->setTranslation(translation);
	data->setRotation(rotation);
	data->setScale(scale);
	return *this;
}

StreamBuffer& StreamBuffer::operator>> (Command& data) {
	uint8_t typ;
	*this >> typ >> data.direction;
	data.type = CommandType(typ);
	return *this;
}