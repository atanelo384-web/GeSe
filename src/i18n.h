// UI localization: English source strings are translated at render time (ImGui text hook).
#pragma once

constexpr int kLanguageCount = 5;
extern const char* const kLanguageNames[kLanguageCount];  // native names
void i18nSetLanguage(int lang);                            // 0 en, 1 ru, 2 uk, 3 de, 4 es
int i18nLanguage();
int i18nSystemDefault();                                   // from the Windows UI language
const char* T(const char* en);                             // explicit lookup for custom-drawn text
