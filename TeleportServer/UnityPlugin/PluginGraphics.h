#pragma once

namespace teleport
{
	class GraphicsManager
	{
	public:
		static void* CreateTextureCopy(void* sourceTexture);
		static void CopyResource(void* target, void* source);
		static void ReleaseResource(void* resource);
		static void AddResourceRef(void* texture);

		static void* mUnityInterfaces;
		static void* mGraphics;
		static void* mGraphicsDevice;
	};
}

