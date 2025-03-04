/************************************************************************************

Filename    :   GlBuffer.cpp
Content     :   OpenGL texture loading.
Created     :   September 30, 2013
Authors     :   John Carmack

Copyright   :   Copyright (c) Facebook Technologies, LLC and its affiliates. All rights reserved.

*************************************************************************************/

#include "GlBuffer.h"
#include "Misc/Log.h"
#include "CompilerUtils.h"
#include <GLES3/gl32.h>

namespace OVRFW {

GlBuffer::GlBuffer() : target(0), buffer(0), size(0) {}

bool GlBuffer::Create(const GlBufferType_t type, const size_t dataSize, const void* data) {
    assert(buffer == 0);

	target = ( ( type == GLBUFFER_TYPE_UNIFORM ) ? GL_UNIFORM_BUFFER : (( type == GLBUFFER_TYPE_STORAGE ) ? GL_SHADER_STORAGE_BUFFER :0) );
	GLenum usage=( ( type == GLBUFFER_TYPE_UNIFORM ) ? GL_STATIC_DRAW :(( type == GLBUFFER_TYPE_STORAGE ) ? GL_DYNAMIC_DRAW :0) );

    size = dataSize;

    glGenBuffers(1, &buffer);
    glBindBuffer(target, buffer);
	glBufferData( target, dataSize, data, usage);
    glBindBuffer(target, 0);

    return true;
}

void GlBuffer::Destroy() {
    if (buffer != 0) {
        glDeleteBuffers(1, &buffer);
        buffer = 0;
    }
}

void GlBuffer::Update(const size_t updateDataSize, const void* data, uint32_t offset ) const
{
    assert(buffer != 0);

	size_t subSize = offset + updateDataSize;
	if ( subSize > size )
    {
	    ALOGE_FAIL(
            "GlBuffer::Update: size overflow %zu specified, %zu allocated\n", updateDataSize, size);
    }

    glBindBuffer(target, buffer);
	glBufferSubData( target, offset, updateDataSize, data );
    glBindBuffer(target, 0);
}

void* GlBuffer::MapBuffer() const {
    assert(buffer != 0);

    void* data = NULL;
    glBindBuffer(target, buffer);
    data = glMapBufferRange(target, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    glBindBuffer(target, 0);

    if (data == NULL) {
        ALOGE_FAIL("GlBuffer::MapBuffer: Failed to map buffer");
    }

    return data;
}

void GlBuffer::UnmapBuffer() const {
    assert(buffer != 0);

    glBindBuffer(target, buffer);
    if (!glUnmapBuffer(target)) {
        ALOGW("GlBuffer::UnmapBuffer: Failed to unmap buffer.");
    }
    glBindBuffer(target, 0);
}

} // namespace OVRFW
