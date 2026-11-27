#pragma once

#include <cstdint>
#include <string>

struct X86Context;

bool GameplayPromptText(uint32_t node, uint32_t localized, std::string& text);
bool PagerUnreadActive();
void SyncPagerPrompt(X86Context& ctx, uint8_t* base);
