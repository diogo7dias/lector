#pragma once

enum class PopupRefresh { Clean, Temporary };

constexpr PopupRefresh popupRefreshMode(PopupRefresh requested = PopupRefresh::Clean) { return requested; }
