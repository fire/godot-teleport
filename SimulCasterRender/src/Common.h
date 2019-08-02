// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once

//C Libraries
#include <iostream>
#include <string>
#include <assert.h>

//STL
#include <vector>
#include <deque>
#include <map>
#include <memory>

//Debug
#define SCR_CERR_BREAK(msg) std::cerr << __FILE__ << "(" << __LINE__ << "): " << msg << std::endl; throw();
#define SCR_CERR(msg)		std::cerr << __FILE__ << "(" << __LINE__ << "): " << msg << std::endl; throw();

#define SCR_COUT_BREAK(msg) std::cout << __FILE__ << "(" << __LINE__ << "): " << msg << std::endl;
#define SCR_COUT(msg)		std::cout << __FILE__ << "(" << __LINE__ << "): " << msg << std::endl;

//Graphics API
namespace scr
{
	enum class API :uint32_t
	{
		D3D11,
		D3D12,
		OPENGL,
		VULKAN
	};
}
