#pragma once

// Private diagnostic only: nonblocking D3D11 timestamps, never used for frame
// publication or backend selection. A disjoint/unavailable result is discarded.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>

#include "first_person_profile.hpp"

namespace devilution::first_person_profile {
class GpuTimer {
	ID3D11Device *device_ = nullptr; // identity only; queries retain their device
	ID3D11Query *disjoint_ = nullptr;
	std::array<ID3D11Query *, 3> stamps_ {};
	bool active_ = false;
	void Release()
	{
		for (auto *&query : stamps_) { if (query) query->Release(); query = nullptr; }
		if (disjoint_) disjoint_->Release();
		disjoint_ = nullptr; device_ = nullptr; active_ = false;
	}
public:
	~GpuTimer() { Release(); }
	void Begin(ID3D11Device *device, ID3D11DeviceContext *context)
	{
		active_ = false;
		if (device != device_ || !disjoint_) {
			Release();
			D3D11_QUERY_DESC desc { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			if (FAILED(device->CreateQuery(&desc, &disjoint_))) return;
			desc.Query = D3D11_QUERY_TIMESTAMP;
			for (auto *&query : stamps_)
				if (FAILED(device->CreateQuery(&desc, &query))) { Release(); return; }
			device_ = device;
		}
		context->Begin(disjoint_);
		context->End(stamps_[0]);
		active_ = true;
	}
	void DrawsComplete(ID3D11DeviceContext *context)
	{
		if (active_) context->End(stamps_[1]);
	}
	void CopiesComplete(ID3D11DeviceContext *context)
	{
		if (!active_) return;
		context->End(stamps_[2]);
		context->End(disjoint_);
	}
	void ReadResult(ID3D11DeviceContext *context)
	{
		if (!active_) return;
		active_ = false;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT info {};
		if (context->GetData(disjoint_, &info, sizeof(info), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK || info.Disjoint || !info.Frequency) return;
		std::array<UINT64, 3> times {};
		for (size_t i = 0; i < times.size(); ++i)
			if (context->GetData(stamps_[i], &times[i], sizeof(times[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
		if (times[1] < times[0] || times[2] < times[1]) return;
		Current.gpuTimestampValid = true;
		Current.gpuDrawTimestampMs = static_cast<double>(times[1] - times[0]) * 1000 / info.Frequency;
		Current.gpuCopyTimestampMs = static_cast<double>(times[2] - times[1]) * 1000 / info.Frequency;
	}
};
inline GpuTimer Timer;
} // namespace devilution::first_person_profile
