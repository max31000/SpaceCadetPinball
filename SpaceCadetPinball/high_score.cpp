#include "pch.h"
#include "high_score.h"

#include "options.h"
#include "pb.h"
#include "score.h"
#include "translations.h"
#include "web_api.h"

bool high_score::dlg_enter_name;
bool high_score::ShowDialog = false;
high_score_entry high_score::DlgData;
std::vector<high_score_entry> high_score::ScoreQueue;
high_score_struct high_score::highscore_table[5];

int high_score::read()
{
	char Buffer[20];

	int checkSum = 0;
	clear_table();
	for (auto position = 0; position < 5; ++position)
	{
		auto& tablePtr = highscore_table[position];

		snprintf(Buffer, sizeof Buffer, "%d", position);
		strcat(Buffer, ".Name");
		auto name = options::GetSetting(Buffer, "");
		strncpy(tablePtr.Name, name.c_str(), sizeof tablePtr.Name);

		snprintf(Buffer, sizeof Buffer, "%d", position);
		strcat(Buffer, ".Score");
		tablePtr.Score = options::get_int(Buffer, tablePtr.Score);

		for (int i = static_cast<int>(strlen(tablePtr.Name)); --i >= 0; checkSum += tablePtr.Name[i])
		{
		}
		checkSum += tablePtr.Score;
	}

	auto verification = options::get_int("Verification", 7);
	if (checkSum != verification)
		clear_table();
	return 0;
}

int high_score::write()
{
	char Buffer[20];

	int checkSum = 0;
	for (auto position = 0; position < 5; ++position)
	{
		auto& tablePtr = highscore_table[position];

		snprintf(Buffer, sizeof Buffer, "%d", position);
		strcat(Buffer, ".Name");
		options::SetSetting(Buffer, tablePtr.Name);

		snprintf(Buffer, sizeof Buffer, "%d", position);
		strcat(Buffer, ".Score");
		options::set_int(Buffer, tablePtr.Score);

		for (int i = static_cast<int>(strlen(tablePtr.Name)); --i >= 0; checkSum += tablePtr.Name[i])
		{
		}
		checkSum += tablePtr.Score;
	}

	options::set_int("Verification", checkSum);
	return 0;
}

void high_score::clear_table()
{
	for (auto& table : highscore_table)
	{
		table.Score = -999;
		table.Name[0] = 0;
	}
}

int high_score::get_score_position(int score)
{
	if (score <= 0)
		return -1;

	for (int position = 0; position < 5; position++)
	{
		if (highscore_table[position].Score < score)
			return position;
	}
	return -1;
}

void high_score::place_new_score_into(high_score_entry data)
{
	if (data.Position >= 0 && data.Position < 5)
	{
		for (int i = 4; i > data.Position; i--)
		{
			highscore_table[i] = highscore_table[i - 1];
		}

		data.Entry.Name[31] = 0;
		highscore_table[data.Position] = data.Entry;
	}
}

void high_score::show_high_score_dialog()
{
	ShowDialog = true;
}

void high_score::show_and_set_high_score_dialog(high_score_entry score)
{
	ScoreQueue.insert(ScoreQueue.begin(), score);
	ShowDialog = true;
}

void high_score::RenderHighScoreDialog()
{
#ifdef __EMSCRIPTEN__
	ProcessQueueWeb();
	return;
#endif
	if (ShowDialog == true)
	{
		ShowDialog = false;
		if (!ImGui::IsPopupOpen(pb::get_rc_string(Msg::HIGHSCORES_Caption)))
		{
			dlg_enter_name = false;
			while (!ScoreQueue.empty())
			{
				DlgData = ScoreQueue.back();
				ScoreQueue.pop_back();
				if (DlgData.Position < 0 || DlgData.Position > 4)
				{
					DlgData.Position = get_score_position(DlgData.Entry.Score);
				}

				if (DlgData.Position != -1)
				{
					dlg_enter_name = true;
					break;
				}
			}

			ImGui::OpenPopup(pb::get_rc_string(Msg::HIGHSCORES_Caption));
		}
	}

	bool unused_open = true, textBoxSubmit = false;
	if (ImGui::BeginPopupModal(pb::get_rc_string(Msg::HIGHSCORES_Caption), &unused_open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (ImGui::BeginTable("table1", 3, ImGuiTableFlags_Borders))
		{
			char buf[36];
			ImGui::TableSetupColumn(pb::get_rc_string(Msg::HIGHSCORES_Rank));
			ImGui::TableSetupColumn(pb::get_rc_string(Msg::HIGHSCORES_Name));
			ImGui::TableSetupColumn(pb::get_rc_string(Msg::HIGHSCORES_Score));
			ImGui::TableHeadersRow();

			for (int offset = 0, row = 0; row < 5; row++)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				snprintf(buf, sizeof buf, "%d", row + 1);
				ImGui::TextUnformatted(buf);

				auto currentRow = &highscore_table[row + offset];
				auto score = currentRow->Score;
				ImGui::TableNextColumn();
				if (dlg_enter_name && DlgData.Position == row)
				{
					offset = -1;
					score = DlgData.Entry.Score;
					ImGui::PushItemWidth(200);

					if (ImGui::IsWindowAppearing())
					{
						ImGui::SetKeyboardFocusHere(0);
					}
					if (ImGui::InputText("", DlgData.Entry.Name, IM_ARRAYSIZE(DlgData.Entry.Name),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
					{
						textBoxSubmit = true;
					}
				}
				else
				{
					ImGui::TextUnformatted(currentRow->Name);
				}

				ImGui::TableNextColumn();
				score::string_format(score, buf);
				ImGui::TextUnformatted(buf);
			}
			ImGui::EndTable();
		}

		if (ImGui::Button(pb::get_rc_string(Msg::GenericOk)) || textBoxSubmit)
		{
			if (dlg_enter_name)
			{
				place_new_score_into(DlgData);
			}
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel)))
			ImGui::CloseCurrentPopup();

		ImGui::SameLine();
		if (ImGui::Button(pb::get_rc_string(Msg::HIGHSCORES_Clear)))
			ImGui::OpenPopup("Confirm");
		if (ImGui::BeginPopupModal("Confirm", nullptr, ImGuiWindowFlags_MenuBar))
		{
			ImGui::TextUnformatted(pb::get_rc_string(Msg::STRING141));
			if (ImGui::Button(pb::get_rc_string(Msg::GenericOk), ImVec2(120, 0)))
			{
				clear_table();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::SameLine();
			if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel), ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::EndPopup();

		// Reenter dialog for the next score in the queue
		if (!ImGui::IsPopupOpen(pb::get_rc_string(Msg::HIGHSCORES_Caption)) && !ScoreQueue.empty())
		{
			ShowDialog = true;
		}
	}
}

// Web build: instead of the ImGui modal, hand the next queued score to the host page and wait
// for bliss_submit_high_score_name / bliss_cancel_high_score. The plain "High Scores" view
// (show_high_score_dialog) is a host dialog fed by bliss_get_high_scores.
void high_score::ProcessQueueWeb()
{
	if (!ShowDialog)
		return;
	ShowDialog = false;
	if (dlg_enter_name)
		return; // still waiting for the host

	while (!ScoreQueue.empty())
	{
		DlgData = ScoreQueue.back();
		ScoreQueue.pop_back();
		if (DlgData.Position < 0 || DlgData.Position > 4)
			DlgData.Position = get_score_position(DlgData.Entry.Score);
		if (DlgData.Position != -1)
		{
			dlg_enter_name = true;
			break;
		}
	}

	if (dlg_enter_name)
	{
		DlgData.Entry.Name[sizeof DlgData.Entry.Name - 1] = 0;
		web_api::HighScoreEntry(DlgData.Entry.Score, DlgData.Position, DlgData.Entry.Name,
		                        static_cast<int>(ScoreQueue.size()));
	}
}

int high_score::SubmitPendingName(const char* name)
{
	if (!dlg_enter_name)
		return 0;

	// Empty name: keep the pre-filled default name ("Player N"), like confirming the original
	// dialog without typing.
	if (!name[0])
		name = DlgData.Entry.Name;
	std::string nameCopy = name;
	name = nameCopy.c_str();

	// Truncate to the 31-byte field without splitting a UTF-8 sequence.
	auto length = strlen(name);
	const auto maxLength = sizeof DlgData.Entry.Name - 1;
	if (length > maxLength)
	{
		length = maxLength;
		while (length > 0 && (static_cast<unsigned char>(name[length]) & 0xC0) == 0x80)
			length--;
	}
	memset(DlgData.Entry.Name, 0, sizeof DlgData.Entry.Name);
	memcpy(DlgData.Entry.Name, name, length);

	auto position = get_score_position(DlgData.Entry.Score);
	dlg_enter_name = false;
	ShowDialog = !ScoreQueue.empty();
	if (position < 0)
		return 0;
	DlgData.Position = position;
	place_new_score_into(DlgData);
	write();
	return position + 1;
}

bool high_score::CancelPendingEntry()
{
	if (!dlg_enter_name)
		return false;
	dlg_enter_name = false;
	ShowDialog = !ScoreQueue.empty();
	return true;
}

void high_score::ClearAndSave()
{
	clear_table();
	write();
}
