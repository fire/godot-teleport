// (C) Copyright 2018-2021 Simul Software Ltd

#pragma once

#include <ClientRender/MemoryUtil.h>

#include "JniUtils.h"

class Android_MemoryUtil : public clientrender::MemoryUtil
{
public:
	Android_MemoryUtil(JNIEnv* env);
	virtual ~Android_MemoryUtil();

	long getAvailableMemory() const override;
	long getTotalMemory() const override;
	void printMemoryStats() const override;

	static void InitializeJNI(JNIEnv* env);
	static bool IsJNIInitialized()
	{
		return mJNIInitialized;
	}

private:
	static bool mJNIInitialized;

	JNIEnv* mEnv;
	jobject mMemoryUtilKt;

	struct JNI {
		jclass memoryUtilClass;
		jmethodID ctorMethod;
		jmethodID getAvailMemMethod;
		jmethodID getTotalMemMethod;
		jmethodID printMemStatsMethod;
	};
	static JNI jni;
};
