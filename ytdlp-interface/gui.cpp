﻿#include "gui.hpp"
#include "icons.hpp"

#include <regex>
#include <codecvt>
#include <algorithm>
#include <nana/gui/filebox.hpp>

settings_t GUI::conf;


GUI::GUI() : themed_form {std::bind(&GUI::apply_theme, this, std::placeholders::_1)}
{
	using namespace nana;

	if(WM_TASKBAR_BUTTON_CREATED = RegisterWindowMessageW(L"TaskbarButtonCreated"))
	{
		msg.make_after(WM_TASKBAR_BUTTON_CREATED, [&](UINT, WPARAM, LPARAM, LRESULT*)
		{
			if(FAILED(i_taskbar.CoCreateInstance(CLSID_TaskbarList)))
				return false;

			if(i_taskbar->HrInit() != S_OK)
			{
				i_taskbar.Release();
				return false;
			}
			msg.umake_after(WM_TASKBAR_BUTTON_CREATED);
			return true;
		});
	}

	msg.make_after(WM_COPYDATA, [&](UINT, WPARAM, LPARAM lparam, LRESULT*)
	{
		auto pcds {reinterpret_cast<PCOPYDATASTRUCT>(lparam)};
		std::wstring url {reinterpret_cast<LPCWSTR>(pcds->lpData), pcds->cbData / 2};
		auto item {lbq.item_from_value(url)};
		if(!item.empty())
		{
			switch(pcds->dwData)
			{
			case YTDLP_POSTPROCESS:
				if(bottoms.at(url).started())
					item.text(3, "processing");
				else return true;
				if(conf.cb_lengthyproc && bottoms.contains(url))
				{
					auto &bottom {bottoms.at(url)};
					if(!bottom.received_procmsg)
					{
						bottom.received_procmsg = true;
						bottom.timer_proc.interval(conf.max_proc_dur);
						bottom.timer_proc.elapse([&, url]
						{
							if(bottoms.contains(url))
							{
								bottom.timer_proc.stop();
								auto next_url {next_startable_url(url)};
								if(!next_url.empty())
									on_btn_dl(next_url);
							}
						});
						bottom.timer_proc.start();
					}
				}
				break;

			case YTDLP_DOWNLOAD:
				item.text(3, "downloading");
				break;
			}
		}
		else if(pcds->dwData == ADD_URL)
			add_url(url);
		return true;
	});

	msg.make_after(WM_SYSCOMMAND, [&](UINT, WPARAM wparam, LPARAM, LRESULT*)
	{
		if(wparam == 1337)
		{
			restore();
			bring_top(true);
			center(1000, 703 - bottoms.current().expcol.collapsed() * 240);
		}
		return true;
	});

	msg.make_after(WM_ENDSESSION, [&](UINT, WPARAM wparam, LPARAM lparam, LRESULT*)
	{
		if(wparam && lparam & ENDSESSION_CLOSEAPP) close();
		return true;
	});

	msg.make_after(WM_KEYDOWN, [&](UINT, WPARAM wparam, LPARAM, LRESULT*)
	{
		if(wparam == 'S')
		{
			if(GetAsyncKeyState(VK_CONTROL) & 0xff00)
			{
				fm_settings();
			}
		}
		if(wparam == 'V')
		{
			if(GetAsyncKeyState(VK_CONTROL) & 0xff00)
			{
				if(api::focus_window() != bottoms.current().com_args)
				{
					l_url.events().mouse_up.emit({}, l_url);
					if(!queue_panel.visible())
						show_queue(true);
				}
			}
		}
		else if(wparam == VK_NUMPAD0)
		{
			if(GetAsyncKeyState(VK_CONTROL) & 0xff00)
			{
				if(is_zoomed(true)) restore();
				center(1000, 703 - bottoms.current().expcol.collapsed() * 240);
			}
		}
		else if(wparam == VK_DELETE)
		{
			if(queue_panel.visible())
			{
				auto sel {lbq.selected()};
				if(!sel.empty())
				{
					if(sel.size() == 1)
					{
						auto fwnd {api::focus_window()};
						auto &bottom {bottoms.current()};
						if(fwnd != bottom.com_args && fwnd != bottom.tbrate)
						{
							outbox.clear(bottom.url);
							queue_remove_item(bottom.url);
						}
					}
					else
					{
						if(sel.size() == lbq.at(sel.front().cat).size())
							queue_remove_all();
						else queue_remove_selected();
					}
				}
			}
		}
		else if(wparam == 'F')
		{
			if(GetAsyncKeyState(VK_CONTROL) & 0xff00 && queue_panel.visible())
			{
				if(bottoms.current().btnfmt_visible())
					fm_formats();
			}
		}
		else if(wparam == 'A')
		{
			if(GetAsyncKeyState(VK_CONTROL) & 0xff00 && queue_panel.visible() && !lbq.first_visible().empty())
			{
				if(api::focus_window() != lbq)
				{
					size_t cat {0};
					auto sel {lbq.selected()};
					if(!sel.empty())
						cat = sel.front().cat;
					auto hsel {lbq.events().selected.connect_front([](const nana::arg_listbox &arg) {arg.stop_propagation(); })};
					lbq.auto_draw(false);
					for(auto item : lbq.at(cat))
						item.select(true);
					lbq.auto_draw(true);
					lbq.events().selected.remove(hsel);
				}
			}
		}
		else if(wparam == VK_F2)
		{
			if(queue_panel.visible() && lbq.selected().size())
			{
				auto &bottom {bottoms.current()};
				if(!bottom.is_ytplaylist && !bottom.is_bcplaylist && !bottom.is_ytchan && !bottom.is_yttab && !bottom.is_bcchan)
					bottom.browse_for_filename();
			}
		}
		else if(wparam == VK_APPS)
		{
			auto curpos {api::cursor_position()};
			api::calc_window_point(*this, curpos); 
			auto pos {btn_qact.pos()};
			auto url {queue_pop_menu(pos.x, pos.y)};
			if(!url.empty())
			{
				lbq.auto_draw(false);
				queue_remove_item(url);
				lbq.auto_draw(true);
			}
		}
		return true;
	});

	msg.make_before(WM_KEYDOWN, [&](UINT, WPARAM wparam, LPARAM, LRESULT*)
	{
		if(queue_panel.visible())
		{
			if(wparam == VK_UP)
			{
				const auto sel {lbq.selected()};
				if(!sel.empty())
				{
					const auto cat {sel.front().cat};
					if(lbq.at(cat).size() > 1)
					{
						if(GetAsyncKeyState(VK_SHIFT) & 0xff00)
						{
							if(!sel.empty() && sel.front().item > 0)
								lbq.at(listbox::index_pair {0, sel.front().item - 1}).select(true, true);
						}
						else if(lbq.at(cat).size() > 1)
							lbq.move_select(true);
						return false;
					}
				}
			}
			else if(wparam == VK_DOWN)
			{
				const auto sel {lbq.selected()};
				if(!sel.empty())
				{
					const auto cat {sel.front().cat};
					if(lbq.at(cat).size() > 1)
					{
						if(GetAsyncKeyState(VK_SHIFT) & 0xff00)
						{
							if(!sel.empty() && sel.back().item < lbq.at(cat).size() - 1)
								lbq.at(listbox::index_pair {0, sel.back().item + 1}).select(true, true);
						}
						else lbq.move_select(false);
						return false;
					}
				}
			}
			else if(wparam == VK_HOME)
			{
				if(!lbq.first_visible().empty())
				{
					const auto sel {lbq.selected()};
					auto cat {lbq.at(sel.empty() ? 0 : sel.front().cat)};
					if(cat.size() > 1)
					{
						auto first_item {cat.at(0)};
						if(GetAsyncKeyState(VK_SHIFT) & 0xff00)
						{
							if(!sel.empty() && sel.front().item > 0)
							{
								for(auto n {sel.front().item}; n != npos; n--)
									cat.at(n).select(true);
								lbq.scroll(false);
							}
						}
						else
						{
							cat.select(false);
							cat.at(0).select(true, true);
						}
						return false;
					}
				}
			}
			else if(wparam == VK_END)
			{
				if(!lbq.first_visible().empty())
				{
					const auto sel {lbq.selected()};
					auto cat {lbq.at(sel.empty() ? 0 : sel.front().cat)};
					if(cat.size() > 1)
					{
						if(GetAsyncKeyState(VK_SHIFT) & 0xff00)
						{
							if(!sel.empty() && sel.back().item < cat.size() - 1)
							{
								for(auto n {sel.back().item}; n < cat.size(); n++)
									cat.at(n).select(true);
								lbq.scroll(true);
							}
						}
						else
						{
							cat.select(false);
							cat.at(cat.size() - 1).select(true, true);
						}
						return false;
					}
				}
			}
		}
		if(wparam == VK_ESCAPE)
		{
			close();
			return false;
		}
		else if(wparam == VK_TAB)
		{
			if(GetAsyncKeyState(VK_CONTROL) & 0xff00)
			{
				if(queue_panel.visible())
					show_output();
				else show_queue();
				api::refresh_window(*this);
				return false;
			}
		}
		return true;
	});

	msg.make_before(WM_GETMINMAXINFO, [&](UINT, WPARAM, LPARAM lparam, LRESULT*)
	{
		auto info {reinterpret_cast<PMINMAXINFO>(lparam)};
		info->ptMinTrackSize.x = conf.cbminw ? 0 : minw;
		info->ptMinTrackSize.y = minh;
		return false;
	});

	msg.make_before(WM_SETFOCUS, [&](UINT, WPARAM, LPARAM lparam, LRESULT*)
	{
		if(conf.cb_add_on_focus)
		{
			auto text {util::get_clipboard_text()};
			if((text.starts_with(L"http://") || text.starts_with(L"https://")) && text.find('\n') == -1)
			{
				if(text.starts_with(LR"(https://www.youtube.com/watch?v=)"))
					if(text.find(L"&list=") == 43)
						text.erase(43);
				auto item {lbq.item_from_value(text)};
				if(item == lbq.at(0).end())
				{
					l_url.update_caption();
					add_url(text);
				}

				if(::widgets::theme::is_dark())
					l_url.fgcolor(::widgets::theme::path_link_fg);
				else l_url.fgcolor(::widgets::theme::path_link_fg);
			}
		}
		return false;
	});

	pwr_can_shutdown = util::pwr_enable_shutdown_privilege();

	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	number_of_processors = sysinfo.dwNumberOfProcessors;

	std::wstring modpath(4096, '\0');
	modpath.resize(GetModuleFileNameW(0, &modpath.front(), modpath.size()));
	self_path = modpath;
	modpath.erase(modpath.rfind('\\'));
	appdir = modpath;

	if(conf.ffmpeg_path.empty())
	{
		if(fs::exists(appdir / "ffmpeg.exe"))
			conf.ffmpeg_path = appdir;
		else if(!conf.ytdlp_path.empty())
		{
			auto tmp {fs::path {conf.ytdlp_path}.replace_filename("ffmpeg.exe")};
			if(fs::exists(tmp))
				conf.ffmpeg_path = tmp.parent_path();
		}
	}

	tmsg.interval(std::chrono::milliseconds {300});
	tmsg.elapse([this]
	{
		if(!tmsg_text.empty())
		{
			if(api::is_window(tmsg_parent))
			{
				::widgets::msgbox mbox {tmsg_parent, tmsg_title};
				mbox.icon(MB_ICONERROR) << tmsg_text;
				tmsg_text.clear();
				tmsg_title.clear();
				mbox();
			}
		}
	});
	tmsg.start();

	tqueue.interval(std::chrono::milliseconds {500});
	tqueue.elapse([this]
	{
		if(conf.cb_clear_done)
		{
			std::vector<std::wstring> completed;

			for(auto &item : lbq.at(0))
			{
				const auto text {item.text(3)};
				if(text == "done")
					completed.push_back(item.value<lbqval_t>().url);
			}

			if(!completed.empty())
			{
				lbq.auto_draw(false);
				autostart_next_item = false;
				for(auto &url : completed)
				{
					if(!bottoms.at(url).dl_thread.joinable())
					{
						auto item {lbq.item_from_value(url)};
						if(item != lbq.at(0).end())
							queue_remove_item(url, false);
					}
				}
				autostart_next_item = true;
				auto sel {lbq.selected()};
				if(sel.size() > 1)
					for(auto n {1}; n < sel.size(); n++)
						lbq.at(sel[n]).select(false);
				lbq.auto_draw(true);
				queue_save();
				if(!queue_panel.visible())
				{
					show_output();
					api::refresh_window(*this);
				}
			}
		}
		if(start_suspend_fm)
		{
			start_suspend_fm = false;
			fm_suspend();
		}
		if(save_queue)
		{
			save_queue = false;
			queue_save();
		}
	});
	tqueue.start();

	t_load_qitem_data.interval(std::chrono::milliseconds {0});
	t_load_qitem_data.elapse([this]
	{
		t_load_qitem_data.stop();
		if(fs::exists(infopath))
			fm_loading();
		else init_qitems();
	});
	t_load_qitem_data.start();

	if(conf.cbtheme == 2)
	{
		if(system_supports_darkmode())
			system_theme(true);
		else conf.cbtheme = 1;
	}
	else if(conf.cbtheme == 0) dark_theme(true);
	if(conf.cbtheme == 1) dark_theme(false);

	if(conf.get_releases_at_startup)
		get_releases(*this);
	caption(title);
	snap(conf.cbsnap);
	make_form();
	RegisterDragDrop(hwnd, this);

	if(system_theme())
		apply_theme(is_system_theme_dark());
	else apply_theme(dark_theme());

	get_versions();
	
	events().unload([&]
	{
		RevokeDragDrop(hwnd);
		conf.zoomed = is_zoomed(true);
		if(conf.zoomed || is_zoomed(false)) restore();
		conf.winrect = nana::rectangle {pos(), size()};
		menu_working = false;
		if(thr_menu.joinable())
			thr_menu.join();
		if(thr.joinable())
			thr.join();
		if(thr_releases.joinable())
			thr_releases.detach();
		if(thr_releases_ffmpeg.joinable())
			thr_releases_ffmpeg.detach();
		if(thr_releases_ytdlp.joinable())
			thr_releases_ytdlp.detach();
		if(thr_versions.joinable())
			thr_versions.detach();
		if(thr_ver_ffmpeg.joinable())
			thr_ver_ffmpeg.detach();
		for(auto &bottom : bottoms)
		{
			auto &bot {*bottom.second};
			if(bot.info_thread.joinable())
			{
				bot.working_info = false;
				bot.info_thread.join();
			}
			if(bot.dl_thread.joinable())
			{
				bot.working = false;
				bot.dl_thread.join();
			}
		}
		if(i_taskbar)
			i_taskbar.Release();

		conf.argset = bottoms.current().com_args.caption();

		conf.unfinished_queue_items.clear();
		unfinished_qitems_data.clear();
		for(auto item : lbq.at(0))
		{
			auto text {item.text(3)};
			if(text != "done" && (text != "error" || text == "error" && conf.cb_save_errors))
			{
				const auto wurl {item.value<lbqval_t>().url};
				const auto url {to_utf8(wurl)};
				conf.unfinished_queue_items.push_back(url);
				auto &j {unfinished_qitems_data[url]};
				auto &bottom {bottoms.at(url)};
				bottom.to_json(j);
				j["columns"]["website"] = item.text(1);
				j["columns"]["status"] = text;
				j["columns"]["format"] = item.text(4);
				j["columns"]["format_note"] = item.text(5);
				j["columns"]["ext"] = item.text(6);
				j["columns"]["fsize"] = item.text(7);
			}
		}
		if(!unfinished_qitems_data.empty())
			std::ofstream {infopath} << unfinished_qitems_data;
	});

	use_ffmpeg_location = !ffmpeg_location_in_conf_file();	

	center(MINW, MINH);
	show_queue(false);
	if(conf.gpopt_hidden)
	{
		auto &expcol {bottoms.current().expcol};
		expcol.events().click.emit({}, expcol);
		if(conf.winrect.empty())
			center();
	}
	auto sz {api::window_outline_size(*this)};
	minw = sz.width;
	minh = sz.height;
	if(!conf.winrect.empty())
	{
		if(!conf.cbminw && conf.winrect.width < minw)
			conf.winrect.width = dpi_scale(minw, conf.dpi);
		else conf.winrect.width = dpi_scale(conf.winrect.width, conf.dpi);
		conf.winrect.height = dpi_scale(conf.winrect.height, conf.dpi);
		const auto maxh {screen {}.from_window(*this).area().dimension().height};
		if(conf.winrect.height < size().height)
			conf.winrect.height = size().height;
		move(conf.winrect);
		size(conf.winrect.dimension());
		sz = api::window_outline_size(*this);
		if(sz.height > maxh)
		{
			sz.height = maxh;
			api::window_outline_size(*this, sz);
		}
	}
	else center(1000, 703);
	bring_top(true);
	if(conf.zoomed) zoom(true);
	no_draw_freeze = false;

	auto m {GetSystemMenu(hwnd, FALSE)};
	if(m)
	{
		MENUITEMINFOA i {0};
		i.cbSize = sizeof MENUITEMINFO;
		i.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID;
		i.fType = MFT_STRING;
		i.wID = 1337;
		i.dwTypeData = (CHAR*)"Reset size and position\tCtrl+Num0";
		InsertMenuItemA(m, SC_CLOSE, FALSE, &i);
		i.dwTypeData = (CHAR*)"Close\tEsc, Alt+F4";
		i.fMask = MIIM_STRING;
		SetMenuItemInfoA(m, SC_CLOSE, FALSE, &i);
	}
}


bool GUI::process_queue_item(std::wstring url)
{
	using widgets::theme;
	auto &bottom {bottoms.at(url)};
	auto &tbpipe {outbox};
	auto &tbpipe_overlay {overlay};
	auto &com_args {bottom.com_args};
	auto &com_rate {bottom.com_rate};
	auto &tbrate {bottom.tbrate};
	auto &btn_ytfmtlist {bottom.btn_ytfmtlist};
	auto &cbmp3 {bottom.cbmp3};
	auto &com_chap {bottom.com_chap};
	auto &cbkeyframes {bottom.cbkeyframes};
	auto &btndl {bottom.btndl};
	auto &prog {bottom.prog};
	auto &working {bottom.working};
	auto &graceful_exit {bottom.graceful_exit};
	const auto argset {com_args.caption_wstring()};

	if(!bottom.started())
	{
		bottom.btndl.caption("Stop download");
		bottom.idx_error = 0;
		auto item {lbq.item_from_value(url)};
		if(item.checked())
		{
			item.check(false);
			item.fgcolor(lbq.fgcolor());
		}
		if(tbpipe.current() == url)
		{
			tbpipe.clear();
			if(tbpipe_overlay.visible() && bottom.btnq.caption().find("queue") != -1)
			{
				tbpipe.show(url);
			}
		}
		else tbpipe.clear(url);
		if(bottom.cbargs.checked())
		{
			const auto args {bottom.com_args.caption()};
			auto idx {com_args.caption_index()};
			if(idx == -1)
			{
				const auto size {com_args.the_number_of_options()};
				com_args.push_back(args);
				com_args.option(size);
				if(size >= 10)
				{
					com_args.erase(0);
					conf.argsets.erase(conf.argsets.begin());
				}
				conf.argsets.push_back(args);
			}
			com_args.option(idx);
			if(conf.common_dl_options)
				bottoms.propagate_args_options(bottom);
		}

		std::wstring cmd {L'\"' + conf.ytdlp_path.wstring() + L'\"'};

		if(bottom.is_ytlink && conf.cb_android)
			cmd += L" --extractor-args youtube:player_client=android";

		if(conf.com_cookies)
			cmd += L" --cookies-from-browser " + com_cookies_options[conf.com_cookies];

		if(!bottom.using_custom_fmt())
		{
			if(bottom.use_strfmt)
			{
				if(bottom.fmt1.empty() && conf.pref_audio == 6)
				{
					auto it {std::find_if(bottom.vidinfo["formats"].begin(), bottom.vidinfo["formats"].end(), [&](const auto &el)
					{
						return el["format"].get<std::string>().rfind(nana::to_utf8(bottom.fmt2)) != -1;
					})};
					if(it != bottom.vidinfo["formats"].end())
					{
						std::string acodec, aext;
						auto &fmt {*it};
						if(fmt.contains("acodec") && fmt["acodec"] != nullptr)
							acodec = fmt["acodec"];
						if(fmt.contains("audio_ext") && fmt["audio_ext"] != nullptr)
							aext = fmt["audio_ext"];
						if(acodec == "opus" && aext != "opus")
							cmd += L" -x";
					}
				}
				else if(bottom.fmt2.find('+') != -1 || bottom.fmt2 == L"mergeall")
					cmd += L" --audio-multistreams";
				if(!bottom.fmt1.empty() && conf.pref_video == 3)
					cmd += L" --remux-video mkv";

				cmd += L" -f " + bottom.strfmt;
			}
			else
			{
				if(bottom.is_ytlink && conf.cb_premium && conf.pref_res <= 4 && (conf.pref_vcodec != 0 && conf.pref_vcodec != 3 || conf.pref_video == 2))
				{
					if(bottom.is_ytchan || bottom.is_yttab || bottom.is_ytplaylist)
					{
						if(conf.pref_res == 4)
							cmd += L" -f \"bv*[format_note*=Premium]+ba / bv*+ba\"";
					}
					else
					{
						std::string fmtid_premium;
						const auto &formats {bottom.vidinfo["formats"]};
						for(auto &el : formats)
						{
							if(el.contains("format_note") && el["format_note"] != nullptr && el["format_note"] == "Premium")
							{
								fmtid_premium = el["format_id"];
								break;
							}
						}
						if(!fmtid_premium.empty())
						{
							int maxres {1080};
							if(conf.pref_res < 4)
							{
								for(auto &el : formats)
								{
									if(el.contains("height") && el["height"] != nullptr)
									{
										int height {el["height"]};
										if(height > maxres)
											maxres = height;
									}
								}
							}
							if(maxres == 1080 && !fmtid_premium.empty())
								cmd += L" -f " + nana::to_wstring(fmtid_premium) + L"+ba";
						}
					}
				}

				if(conf.pref_video == 3 && bottom.vidinfo_contains("vcodec") && bottom.vidinfo["vcodec"] != "none")
					cmd += L" --remux-video mkv";

				std::wstring strpref {L" -S \""};
				if(conf.pref_res)
					strpref += L"res:" + com_res_options[conf.pref_res];
				if(conf.pref_video && conf.pref_video != 3)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"vext:" + com_video_options[conf.pref_video];
				}
				if(conf.pref_audio)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"aext:" + com_audio_options[conf.pref_audio];
				}
				if(conf.pref_fps)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"fps";
				}
				if(conf.pref_vcodec)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"vcodec:" + com_vcodec_options[conf.pref_vcodec];
				}
				if(conf.pref_acodec)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"acodec:" + com_acodec_options[conf.pref_acodec];
				}
				if(strpref.size() > 5)
					cmd += strpref + L"\" ";
			}
		}

		if(cmd.back() != ' ')
			cmd.push_back(' ');

		if(com_chap.option() == 1 && argset.find(L"--embed-chapters") == -1)
			cmd += L"--embed-chapters ";
		if(conf.cbsubs && argset.find(L"--embed-subs") == -1)
			cmd += L"--embed-subs ";
		if(conf.cbthumb && argset.find(L"--embed-thumbnail") == -1)
			cmd += L"--embed-thumbnail ";
		if(conf.cbtime && argset.find(L"--no-mtime") == -1)
			cmd += L"--no-mtime ";
		if(conf.cbkeyframes && argset.find(L"--force-keyframes-at-cuts") == -1)
			cmd += L"--force-keyframes-at-cuts ";
		if(!conf.ffmpeg_path.empty() && conf.ffmpeg_path != conf.ytdlp_path.parent_path())
			if(argset.find(L"--ffmpeg-location") == -1 && use_ffmpeg_location)
				cmd += L"--ffmpeg-location \"" + conf.ffmpeg_path.wstring() + L"\" ";
		if(tbrate.to_double() && argset.find(L"-r ") == -1)
			cmd += L"-r " + tbrate.caption_wstring() + (com_rate.option() ? L"M " : L"K ");
		if(cbmp3.checked() && argset.find(L"--audio-format") == -1)
		{
			cmd += L"-x --audio-format mp3 ";
			if(argset.find(L"--audio-quality") == -1) 
				L"--audio-quality 0 ";
		}
		if(com_chap.option() == 2 && argset.find(L"--split-chapters") == -1)
			cmd += L"--split-chapters -o chapter:\"" + bottom.outpath.wstring() + L"\\%(title)s - %(section_number)s -%(section_title)s.%(ext)s\" ";
		if(bottom.cbargs.checked() && !argset.empty())
			cmd += argset + L" ";
		for(auto &section : bottom.sections)
		{
			auto arg
			{
				L"--download-sections *" + (section.first.empty() ? L"0" : section.first) + L'-' +
				(section.second.empty() || section.second == L"0" ? L"inf" : section.second) + L' '
			};
			cmd += arg;
		}

		if(bottom.is_ytlink || bottom.is_ytchan)
		{
			std::wstring mark, remove;
			if(conf.cb_sblock_mark && !conf.sblock_mark.empty())
			{
				if(conf.sblock_mark.front() == 0)
				{
					mark = L"all";
				}
				else if((sblock_cats_mark.size() - 1) / 2 < conf.sblock_mark.size())
				{
					mark = L"all";
					for(int n{1}; n < sblock_cats_mark.size(); n++)
					{
						if(std::find(conf.sblock_mark.begin(), conf.sblock_mark.end(), n) == conf.sblock_mark.end())
							mark += L",-" + sblock_cats_mark[n];
					}
				}
				else
				{
					for(auto idx : conf.sblock_mark)
						mark += sblock_cats_mark[idx] + L',';
					mark.pop_back();
				}
			}

			if(conf.cb_sblock_remove && !conf.sblock_remove.empty())
			{
				if(conf.sblock_remove.front() == 0)
				{
					remove = L"all";
				}
				else if((sblock_cats_remove.size() - 1) / 2 < conf.sblock_remove.size())
				{
					remove = L"all";
					for(int n {1}; n < sblock_cats_remove.size(); n++)
					{
						if(std::find(conf.sblock_remove.begin(), conf.sblock_remove.end(), n) == conf.sblock_remove.end())
							remove += L",-" + sblock_cats_remove[n];
					}
				}
				else
				{
					for(auto idx : conf.sblock_remove)
						remove += sblock_cats_remove[idx] + L',';
					remove.pop_back();
				}
			}

			if(!mark.empty() && bottom.com_args.caption().find("--sponsorblock-mark ") == -1)
				cmd += L"--sponsorblock-mark " + mark + L' ';
			if(!remove.empty() && bottom.com_args.caption().find("--sponsorblock-remove ") == -1)
				cmd += L"--sponsorblock-remove " + remove + L' ';
		}

		if(conf.cb_proxy && !conf.proxy.empty() && bottom.com_args.caption().find("--proxy ") == -1)
			cmd += L"--proxy " + conf.proxy + L' ';

		auto display_cmd {cmd};

		cmd += L"--encoding UTF-8 --progress-delta .8 ";

		std::wstringstream ss;
		ss << hwnd;
		auto strhwnd {ss.str()};
		strhwnd.erase(0, strhwnd.find_first_not_of('0'));
		cmd += L"--exec \"after_video:\\\"" + self_path.wstring() + L"\\\" ytdlp_status " + std::to_wstring(YTDLP_POSTPROCESS)
			+ L" " + strhwnd + L" \\\"" + url + L"\\\"\" ";
		cmd += L"--exec \"before_dl:\\\"" + self_path.wstring() + L"\\\" ytdlp_status " + std::to_wstring(YTDLP_DOWNLOAD)
			+ L" " + strhwnd + L" \\\"" + url + L"\\\"\" ";
		fs::path tempfile;
		if(!bottom.is_ytplaylist && !bottom.is_ytchan && !bottom.is_bcplaylist)
		{
			tempfile = fs::temp_directory_path() / std::tmpnam(nullptr);
			cmd += L"--print-to-file after_move:filepath " + tempfile.wstring();
		}
		std::wstring cmd2;
		if(!bottom.cbargs.checked() || argset.find(L"-P ") == -1)
		{
			auto wstr {bottom.outpath.wstring()};
			if(wstr.find(' ') == -1)
				cmd2 += L" -P " + wstr;
			else cmd2 += L" -P \"" + wstr + L"\"";
		}
		if((!bottom.cbargs.checked() || argset.find(L"-o ") == -1) && !conf.output_template.empty())
		{
			std::wstring folder;
			if(bottom.is_ytplaylist)
			{
				if(conf.cb_playlist_folder)
					folder = L"%(playlist_title)s\\";
				if(!conf.playlist_indexing.empty())
				{
					if(conf.cb_zeropadding && bottom.playlist_selection.size() > 9)
					{
						auto pos {conf.playlist_indexing.find(L"%(playlist_index)d")};
						if(pos != -1)
						{
							auto str1 {conf.playlist_indexing.substr(0, pos)},
								str2 {conf.playlist_indexing.substr(pos + 18)},
								str_padval {std::to_wstring(std::to_string(bottom.playlist_selection.size()).size())},
								newstr {str1 + L"%(playlist_index)0" + str_padval + L'd' + str2};
							cmd2 += L" -o \"" + folder + newstr + conf.output_template + L'\"';
						}
					}
					else cmd2 += L" -o \"" + folder + conf.playlist_indexing + conf.output_template + L'\"';
				}
				else cmd2 += L" -o \"" + folder + conf.output_template + L'\"';
			}
			else if(bottom.is_bcplaylist || bottom.is_bcchan)
			{
				if(conf.cb_playlist_folder)
					folder = L"%(artist)s\\%(album)s\\";
				cmd2 += L" -o \"" + folder + conf.output_template_bandcamp + L'\"';
			}
			else if(bottom.is_ytchan)
			{
				if(conf.cb_playlist_folder)
					folder = L"%(playlist_title)s\\";
				cmd2 += L" -o \"" + folder + conf.output_template + L'\"';
			}
			else if(bottom.sections.size() > 1)
			{
				if(bottom.outfile.empty())
				{
					cmd2 += L" -o \"" + conf.output_template;
					if(cmd2.rfind(L".%(ext)s") == cmd2.size() - 8)
						cmd2.erase(cmd2.size() - 8);
				}
				else cmd2 += L" -o \"" + bottom.outfile.filename().wstring();
				cmd2 += L" - section %(section_start)d to %(section_end)d.%(ext)s\"";
			}
			else 
			{
				if(bottom.outfile.empty())
					cmd2 += L" -o \"" + conf.output_template + L'\"';
				else cmd2 += L" -o \"" + bottom.outfile.filename().wstring() + L'\"';
			}
		}
		if((bottom.is_ytplaylist || bottom.is_bcplaylist || bottom.is_scplaylist) && !bottom.playsel_string.empty())
			cmd2 += L" -I " + bottom.playsel_string + L" --compat-options no-youtube-unavailable-videos";
		cmd2 += L" \"" + url + L'\"';
		display_cmd += cmd2;
		cmd += cmd2;
		//display_cmd = cmd;

		if(tbpipe.current() == url)
			tbpipe.clear();

		bottom.dl_thread = std::thread([&, tempfile, display_cmd, cmd, url]
		{
			working = true;
			auto ca {tbpipe.colored_area_access()};
			bool ca_change {false};
			if(outbox.current() == url)
			{
				ca->clear();
				ca_change = true;
			}
			if(fs::exists(conf.ytdlp_path))
				tbpipe.append(url, L"[GUI] executing command line: " + display_cmd + L"\n\n");
			else tbpipe.append(url, L"ytdlp.exe not found: " + conf.ytdlp_path.wstring());
			auto p {ca_change ? ca->get(0) : nullptr};
			if(ca_change)
				p->count = 1;
			if(!fs::exists(conf.ytdlp_path))
			{
				if(ca_change)
					p->fgcolor = theme::is_dark() ? nana::color {"#f99"} : nana::color {"#832"};
				nana::API::refresh_window(tbpipe);
				btndl.caption("Start download");
				if(bottom.dl_thread.joinable())
					bottom.dl_thread.detach();
				return;
			}
			if(ca_change)
				p->fgcolor = theme::is_dark() ? theme::path_link_fg : nana::color {"#569"};
			nana::API::refresh_window(tbpipe);
			ULONGLONG prev_val {0};
			bool playlist_progress {false};

			auto cb_progress = [&, url](ULONGLONG completed, ULONGLONG total, std::string text, int playlist_completed, int playlist_total)
			{
				if(text.find("Unknown B/s") != -1)
					return;

				if(playlist_total && !playlist_progress)
				{
					playlist_progress = true;
					prog.amount(playlist_total);
				}
				while(text.find_last_of("\r\n") != -1)
					text.pop_back();
				auto item {lbq.item_from_value(url)};
				if(total != -1)
				{
					auto strpct {(std::stringstream {} << static_cast<double>(completed) / 10).str() + '%'};
					if(playlist_progress)
					{
						auto strprog {"[" + std::to_string(playlist_completed + 1) + "/" + std::to_string(playlist_total) + "] "};
						item.text(3, strprog + strpct);
						if(i_taskbar && lbq.at(0).size() == 1)
							i_taskbar->SetProgressValue(hwnd, playlist_completed, playlist_total > 1 ? playlist_total - 1 : playlist_total);
					}
					else 
					{
						item.text(3, strpct);
						if(i_taskbar && lbq.at(0).size() == 1)
							i_taskbar->SetProgressValue(hwnd, completed, total);
					}
				}
				if(completed < 1000 && total != -1)
				{
					if(playlist_progress)
						text = "[" + std::to_string(playlist_completed + 1) + " of " + std::to_string(playlist_total) + "]\t" + text;
					prog.caption(text);
				}
				else
				{
					if(prev_val)
					{
						if(text == "[ExtractAudio]" || text.find("[Merger]") == 0 || text.find("[Fixup") == 0)
						{
							COPYDATASTRUCT cds;
							cds.dwData = YTDLP_POSTPROCESS;
							cds.cbData = url.size() * 2;
							cds.lpData = const_cast<std::wstring&>(url).data();
							SendMessageW(hwnd, WM_COPYDATA, NULL, reinterpret_cast<LPARAM>(&cds));

							if(text.find("[Merger]") == 0)
							{
								auto pos {text.find('\"')};
								if(pos++ != -1)
								{
									auto pos2 {text.find('\"', pos)};
									if(pos2 != -1)
									{
										try { bottom.merger_path = fs::u8path(text.substr(pos, pos2 - pos)); }
										catch(...) { bottom.merger_path.clear(); }
									}
								}
							}
						}
						else if(total != -1)
						{
							auto pos {text.find_last_not_of(" \t")};
							if(text.size() > pos)
								text.erase(pos + 1);
							if(playlist_progress)
							{
								prog.shadow_progress(1000, completed);
								prog.value(playlist_completed + 1);
								auto strprog {"[" + std::to_string(playlist_completed + 1) + " of " + std::to_string(playlist_total) + "]\t"};
								prog.caption(strprog + text);
							}
							else prog.caption(text);
						}
					}
					if(total == -1 && text.find("[download]") == 0)
					{
						auto pos {text.find("has already been downloaded")};
						if(pos != -1)
						{
							try { bottom.download_path = fs::u8path(text.substr(11, pos - 2)); }
							catch(...) { bottom.download_path.clear(); }
						}
						else
						{
							pos = text.find("Destination: ");
							if(pos != -1)
							{
								try { bottom.download_path = fs::u8path(text.substr(24)); }
								catch(...) { bottom.download_path.clear(); }
							}
							else bottom.download_path.clear();
						}
						return;
					}
				}
				if(playlist_progress)
					prog.shadow_progress(1000, completed);
				else if(completed <= 1000 && (completed > prog.value() || completed == 0 || prog.value() - completed > 50))
					prog.value(completed);
				prev_val = completed;
			};

			auto item {lbq.item_from_value(url)};
			prog.value(0);
			prog.caption("");
			if(i_taskbar && lbq.at(0).size() == 1) 
				i_taskbar->SetProgressState(hwnd, TBPF_NORMAL);
			if(!item.empty())
				item.text(3, "started");
			auto cb_append = [url, this](std::string text, bool keyword)
			{
				if(keyword)
					outbox.set_keyword(text);
				else outbox.append(url, text);
			};
			bottom.printed_path.clear();
			bottom.merger_path.clear();
			bottom.download_path.clear();
			auto outpath {bottom.outpath};
			auto res {util::run_piped_process(cmd, &working, cb_append, cb_progress, &graceful_exit, tempfile.filename().string())};
			bottom.received_procmsg = false;
			if(!tempfile.empty() && fs::exists(tempfile))
			{
				if(std::ifstream is {tempfile, std::ios::binary | std::ios::ate})
				{
					auto size {is.tellg()};
					std::string str (size, '\0');
					is.seekg(0);
					if(is.read(&str.front(), size))
					{
						while(str.find_last_of("\r\n") != -1)
							str.pop_back();
						bottom.printed_path = fs::u8path(str);
					}
				}
				std::error_code ec;
				fs::remove(tempfile, ec);
			}
			else if(bottom.is_ytplaylist && bottom.playlist_info.contains("title") && bottom.playlist_info["title"] != nullptr)
			{
				bottom.printed_path = bottom.outpath / bottom.playlist_info["title"].get<std::string>();
			}
			else if(bottom.is_bcplaylist)
			{
				// todo
			}

			if(working && bottom.index > 0)
			{
				if(bottom.timer_proc.started())
					bottom.timer_proc.stop();
				item = lbq.item_from_value(url);
				if(res == "failed")
				{
					item.text(3, "error");
					auto progtext {prog.progress::caption()};
					auto pos1 {progtext.find('[')};
					if(pos1 != -1)
					{
						auto pos2 {progtext.find(" of ")};
						if(pos2 != -1)
						{
							auto pos3 {progtext.find(']')};
							if(pos3 != -1 && pos1 < pos2 && pos2 < pos3)
							{
								auto strval {progtext.substr(++pos1, pos2 - pos1)};
								try { bottom.idx_error = std::stoi(strval); }
								catch(...) { }
							}
						}
					}
				}
				else item.text(3, "done");

				taskbar_overall_progress();
				if(i_taskbar && lbq.at(0).size() == 1)
					i_taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
				btndl.enabled(true);
				tbpipe.append(url, "\n[GUI] " + conf.ytdlp_path.filename().string() + " process has exited\n");
				if(tbpipe.current() == url)
				{
					ca = tbpipe.colored_area_access();
					p = ca->get(tbpipe.text_line_count() - 2);
					p->count = 1;
					p->fgcolor = theme::is_dark() ? theme::path_link_fg : nana::color {"#569"};
					nana::API::refresh_window(tbpipe);
				}
				btndl.caption("Start download");
				auto next_url {next_startable_url(url)};
				if(!next_url.empty())
					on_btn_dl(next_url);
				else if(pwr_shutdown || pwr_hibernate || pwr_sleep)
				{
					auto items_currently_downloading {0};
					for(auto item : lbq.at(0))
					{
						const auto text {item.text(3)};
						if(text == "downloading" || (text.find('%') != -1 && text.find("stopped") == -1) || text == "started")
							items_currently_downloading++;
					}
					if(!items_currently_downloading)
						start_suspend_fm = true;
				}
				if(working && bottom.dl_thread.joinable())
					bottom.dl_thread.detach();
				if(close_when_finished)
					close();
			}
		});
		return true; // started
	}
	else
	{
		btndl.enabled(false);
		if(bottom.timer_proc.started())
			bottom.timer_proc.stop();
		if(bottom.dl_thread.joinable())
		{
			working = false;
			bottom.dl_thread.join();
		}
		bottom.received_procmsg = false;
		const auto fname {conf.ytdlp_path.filename().string()};
		if(graceful_exit)
			tbpipe.append(url, "\n[GUI] " + fname + " process was ended gracefully via Ctrl+C signal\n");
		else tbpipe.append(url, "\n[GUI] " + fname + " process was ended forcefully via WM_CLOSE message\n");
		auto item {lbq.item_from_value(url)};
		auto text {item.text(3)};
		auto pos {text.find(']')};
		if(pos != -1)
			item.text(3, "stopped (" + text.substr(1, pos-1) + ")");
		else if(text.find('%') != -1)
			item.text(3, "stopped (" + text + ")");
		else item.text(3, "stopped");
		if(tbpipe.current() == url)
		{
			auto ca {tbpipe.colored_area_access()};
			auto p {ca->get(tbpipe.text_line_count() - 2)};
			p->count = 1;
			if(graceful_exit)
				p->fgcolor = theme::is_dark() ? theme::path_link_fg : nana::color {"#569"};
			else p->fgcolor = theme::is_dark() ? nana::color {"#f99"} : nana::color {"#832"};
		}
		nana::API::refresh_window(tbpipe);
		btndl.enabled(true);
		btndl.caption("Start download");
		if(conf.cb_autostart)
		{
			auto next_url {next_startable_url(url)};
			if(!next_url.empty())
				on_btn_dl(next_url);
		}
		return false; // stopped
	}
}


void GUI::add_url(std::wstring url, bool refresh, bool saveq)
{
	using namespace nana;

	auto item {lbq.item_from_value(url)};

	if(item == lbq.at(0).end() || refresh)
	{
		auto stridx {std::to_string(lbq.at(0).size() + 1)};
		if(refresh)
		{
			auto status_text {item.text(3)};
			for(int n {1}; n < item.columns(); n++)
				item.text(n, "...");
			item.text(3, status_text);
		}
		else 
		{
			lbq.at(0).append({stridx, "...", "...", "queued", "...", "...", "...", "..."});
			lbq.at(0).back().value(lbqval_t {url, nullptr});
			adjust_lbq_headers();
		}

		auto &bottom {bottoms.add(url)};
		if(!refresh)
		{
			if(conf.common_dl_options)
				bottom.gpopt.caption("Download options");
			else bottom.gpopt.caption("Download options for queue item #" + stridx);
			bottom.show_btncopy(!conf.common_dl_options);
			if(lbq.at(0).size() == 1)
				lbq.item_from_value(url).select(true);
			else if(!conf.common_dl_options)
				bottom.show_btncopy(true);
		}
		else
		{
			bottom.vidinfo.clear();
			bottom.playlist_info.clear();
		}

		bottom.info_thread = std::thread([&, url, refresh, saveq]
		{
			bottom.working_info = true;
			static std::atomic_int active_threads {0};
			while(active_threads >= number_of_processors)
			{
				Sleep(100);
				if(!bottom.working_info)
				{
					if(bottom.info_thread.joinable())
						bottom.info_thread.detach();
					return;
				}
			}
			active_threads++;

			auto favicon_url {lbq.favicon_url_from_value(url)};
			if(!favicon_url.empty())
			{
				auto item {lbq.item_from_value(url)};
				std::function<void(paint::image&)> cbfn = [favicon_url, url, this](paint::image &img)
				{
					auto item {lbq.item_from_value(url)};
					if(item != lbq.at(0).end())
					{
						item.value<lbqval_t>().pimg = &img;
						api::refresh_window(lbq);
					}
				};
				favicons[favicon_url].add(favicon_url, cbfn);
			}

			if(!unfinished_qitems_data.empty() && unfinished_qitems_data.contains(to_utf8(url)))
			{
				const auto url8 {to_utf8(url)};
				const auto &j {unfinished_qitems_data[url8]};
				bottom.from_json(j);
				if(j.contains("columns"))
				{
					auto item {lbq.item_from_value(url)};
					item.text(1, j["columns"]["website"].get<std::string>());
					std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> u16conv;
					auto media_title {bottom.media_title};
					if(!is_utf8(media_title))
					{
						std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> u16conv;
						auto u16str {u16conv.from_bytes(media_title)};
						std::wstring wstr(u16str.size(), L'\0');
						memcpy(&wstr.front(), &u16str.front(), wstr.size() * 2);
						std::wstring_convert<std::codecvt_utf8<wchar_t>> u8conv;
						media_title = u8conv.to_bytes(wstr);
					}
					if(media_title.empty())
					{
						if(j["columns"].contains("title"))
							item.text(2, j["columns"]["title"].get<std::string>());
						else item.text(2, "!!! failed to restore the media title !!!");
					}
					else item.text(2, media_title);
					item.text(3, j["columns"]["status"].get<std::string>());
					if(item.text(3) == "skip")
					{
						item.check(true);
						lbq.refresh_theme();
					}
					item.text(4, j["columns"]["format"].get<std::string>());
					item.text(5, j["columns"]["format_note"].get<std::string>());
					item.text(6, j["columns"]["ext"].get<std::string>());
					item.text(7, j["columns"]["fsize"].get<std::string>());
				}
				unfinished_qitems_data.erase(url8);
			}
			else
			{
				std::wstring fmt_sort {' '}, strpref {L" -S \""}, cookies;
				if(conf.pref_res)
					strpref += L"res:" + com_res_options[conf.pref_res];
				if(conf.pref_video)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"vext:" + com_video_options[conf.pref_video];
				}
				if(conf.pref_audio)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"aext:" + com_audio_options[conf.pref_audio];
				}
				if(conf.pref_fps)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"fps";
				}
				if(conf.pref_vcodec)
				{
					if(strpref.size() > 5)
						strpref += ',';
					strpref += L"vcodec:" + com_vcodec_options[conf.pref_vcodec];
				}
				if(strpref.size() > 5)
					fmt_sort = strpref + L"\" ";

				if(conf.com_cookies)
					cookies = L" --cookies-from-browser " + com_cookies_options[conf.com_cookies] + L" ";

				std::string media_info, media_website {"---"}, media_title, format_id {"---"}, format_note {"---"}, ext {"---"}, filesize {"---"};
				auto json_error = [&](const nlohmann::detail::exception &e)
				{
					media_title = "Can't parse the JSON data produced by yt-dlp! See output for details.";
					lbq.item_from_value(url).text(3, "error");
					outbox.caption(e.what() + std::string {"\n\n"} + media_info, url);
					if(outbox.current() == url)
					{
						auto ca {outbox.colored_area_access()};
						ca->clear();
						auto p {ca->get(0)};
						p->fgcolor = ::widgets::theme::is_dark() ? ::widgets::theme::path_link_fg : color {"#569"};
					}
					if(!queue_panel.visible() && overlay.visible())
					{
						outbox.widget::show();
						overlay.hide();
					}
				};

				std::wstring force_android {conf.cb_android ? L"--extractor-args youtube:player_client=android " : L""};

				if(bottom.is_ytlink && !bottom.is_ytchan || bottom.is_bcplaylist || bottom.is_scplaylist)
				{
					if(bottom.is_ytplaylist || bottom.is_bcplaylist || bottom.is_scplaylist)
					{
						std::wstring flat {bottom.is_scplaylist ? L"" : L"--flat-playlist "};
						std::wstring compat_options {bottom.is_ytplaylist ? L" --compat-options no-youtube-unavailable-videos" : L""},
							cmd {L" --no-warnings " + cookies + flat + L"-J" + compat_options + L" \"" + url + L'\"'};
						if(conf.cb_proxy && !conf.proxy.empty())
							cmd = L" --proxy " + conf.proxy + cmd;
						bottom.cmdinfo = conf.ytdlp_path.filename().wstring() + cmd;
						media_info = util::run_piped_process(L'\"' + conf.ytdlp_path.wstring() + L'\"' + cmd, &bottom.working_info);
						bool incomplete_data_received {false};
						if(media_info.starts_with("ERROR: Incomplete data received"))
						{
							auto pos {media_info.find('{')};
							if(pos != -1)
							{
								media_info.erase(0, pos);
								incomplete_data_received = true;
							}
						}
						else if(media_info.starts_with("ERROR: "))
						{
							auto pos {media_info.find('{')};
							if(pos != -1)
								media_info.erase(pos);
						}
						if(!media_info.empty() && media_info.front() == '{')
						{
							try { bottom.playlist_info = nlohmann::json::parse(media_info); }
							catch(nlohmann::detail::exception e)
							{
								bottom.playlist_info.clear();
								if(lbq.item_from_value(url) != lbq.at(0).end())
									json_error(e);
							}
							if(!bottom.playlist_info.empty())
							{
								if(incomplete_data_received)
								{
									auto it {bottom.playlist_info["entries"].end()};
									bottom.playlist_info["entries"].erase(--it);
								}
								std::string URL {bottom.playlist_info["entries"][0]["url"]};
								cmd = L" --no-warnings -j " + cookies + (bottom.is_ytplaylist ? force_android : L"") + fmt_sort + to_wstring(URL);
								media_info = {util::run_piped_process(L'\"' + conf.ytdlp_path.wstring() + L'\"' + cmd, &bottom.working_info)};
								if(!media_info.empty() && media_info.front() == '{')
								{
									try { bottom.vidinfo = nlohmann::json::parse(media_info); }
									catch(nlohmann::detail::exception e)
									{
										bottom.vidinfo.clear();
										if(lbq.item_from_value(url) != lbq.at(0).end())
											json_error(e);
									}
									if(!bottom.vidinfo.empty())
										bottom.show_btnfmt(true);
								}
								else bottom.playlist_vid_cmdinfo = conf.ytdlp_path.filename().wstring() + cmd;
							}
						}
					}
					else // YouTube video
					{
						std::wstring cmd {L" --no-warnings -j " + cookies + force_android + (conf.output_template.empty() ? L"" : L"-o \"" +
							conf.output_template + L'\"') + fmt_sort + L'\"' + url + L'\"'};
						if(conf.cb_proxy && !conf.proxy.empty())
							cmd = L" --proxy " + conf.proxy + cmd;
						bottom.cmdinfo = conf.ytdlp_path.filename().wstring() + cmd;
						media_info = util::run_piped_process(L'\"' + conf.ytdlp_path.wstring() + L'\"' + cmd, &bottom.working_info);
						if(media_info.find("ERROR:") == 0)
						{
							auto pos {media_info.find("This live event will begin in ")};
							if(pos != -1)
							{
								auto strtime {media_info.substr(pos + 30, media_info.rfind('.') - pos - 30)};
								lbq.item_from_value(url).text(1, "youtube.com");
								lbq.item_from_value(url).text(2, "[live event scheduled to begin in " + strtime + ']');
								lbq.item_from_value(url).text(4, "---");
								lbq.item_from_value(url).text(5, "---");
								lbq.item_from_value(url).text(6, "---");
								lbq.item_from_value(url).text(7, "---");
								bottom.vidinfo.clear();
								if(--active_threads == 0)
									queue_save();
								if(bottom.working_info)
									bottom.info_thread.detach();
								return;
							}
						}
						auto pos {media_info.rfind('}')};
						if(pos != -1)
							media_info.erase(pos + 1);
						if(bottom.working_info)
						{
							auto pos {media_info.find("{\"id\":")};
							if(pos != -1)
							{
								media_info.erase(0, pos);
								try { bottom.vidinfo = nlohmann::json::parse(media_info); }
								catch(nlohmann::detail::exception e)
								{
									bottom.vidinfo.clear();
									if(lbq.item_from_value(url) != lbq.at(0).end())
										json_error(e);
								}
							}
						}
					}
				}
				else if(bottom.is_ytchan || bottom.is_bcchan)
				{
					std::wstring cmd {L" --no-warnings --flat-playlist -I :0 -J \"" + cookies + url + L'\"'};
					if(bottom.is_ytplaylist)
						cmd = L" --no-warnings --flat-playlist -J \"" + cookies + url + L'\"';
					if(conf.cb_proxy && !conf.proxy.empty())
						cmd += L" --proxy " + conf.proxy + cmd;
					bottom.cmdinfo = conf.ytdlp_path.filename().wstring() + cmd;
					media_info = util::run_piped_process(L'\"' + conf.ytdlp_path.wstring() + L'\"' + cmd, &bottom.working_info);
					try { bottom.playlist_info = nlohmann::json::parse(media_info); }
					catch(nlohmann::detail::exception e)
					{
						bottom.playlist_info.clear();
						if(lbq.item_from_value(url) != lbq.at(0).end())
							json_error(e);
					}
					if(!bottom.playlist_info.empty())
					{
						std::string tab {bottom.is_bcchan ? "[user page] " : "[whole channel] "};
						std::vector<std::string> tabs {"videos", "featured", "playlists", "shorts", "streams", "community", "podcasts"};
						if(bottom.is_ytchan)
							for(const auto &t : tabs)
								if(url.rfind(L"/" + to_wstring(t)) == url.size() - t.size() - 1)
								{
									tab = "[channel tab] ";
									if(t == "videos" || t == "shorts" || t == "streams" || t == "podcasts")
										bottom.is_yttab = true;
									break;
								}
						auto media_title {bottom.playlist_info["title"].get<std::string>()};
						bottom.media_title = media_title;

						if(!is_utf8(media_title))
						{
							std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> u16conv;
							auto u16str {u16conv.from_bytes(media_title)};
							std::wstring wstr(u16str.size(), L'\0');
							memcpy(&wstr.front(), &u16str.front(), wstr.size() * 2);
							std::wstring_convert<std::codecvt_utf8<wchar_t>> u8conv;
							media_title = u8conv.to_bytes(wstr);
						}

						if(bottom.is_ytplaylist)
						{
							std::string URL {bottom.playlist_info["entries"][0]["url"]};
							cmd = L" --no-warnings -j " + cookies + fmt_sort + to_wstring(URL);
							media_info = {util::run_piped_process(L'\"' + conf.ytdlp_path.wstring() + L'\"' + cmd, &bottom.working_info)};
							if(!media_info.empty() && media_info.front() == '{')
							{
								try { bottom.vidinfo = nlohmann::json::parse(media_info); }
								catch(nlohmann::detail::exception e)
								{
									bottom.vidinfo.clear();
									if(lbq.item_from_value(url) != lbq.at(0).end())
										json_error(e);
								}
								if(!bottom.vidinfo.empty())
									bottom.show_btnfmt(true);
							}
							else bottom.playlist_vid_cmdinfo = conf.ytdlp_path.filename().wstring() + cmd;
						}
						else
						{
							lbq.item_from_value(url).text(1, bottom.is_bcchan ? "bandcamp.com" : "youtube.com");
							lbq.item_from_value(url).text(2, tab + media_title);
							lbq.item_from_value(url).text(4, "---");
							lbq.item_from_value(url).text(5, "---");
							lbq.item_from_value(url).text(6, "---");
							lbq.item_from_value(url).text(7, "---");
							bottom.vidinfo.clear();
							if(vidsel_item.m && lbq.item_from_value(url).selected())
							{
								auto &m {*vidsel_item.m};
								for(int n {0}; n < m.size(); n++)
								{
									m.enabled(n, true);
									if(m.text(n) == "Select formats" && !bottom.btnfmt_visible())
										m.erase(n--);
								}
								api::refresh_window(m.handle());
								vidsel_item.m = nullptr;
							}

							if(--active_threads == 0)
								queue_save();
							if(bottom.working_info)
								bottom.info_thread.detach();
							return;
						}
					}
				}
				else // not YouTube
				{
					std::wstring fsize_approx;
					if(ver_ytdlp > version_t {2023, 11, 13})
						fsize_approx = L" --compat-options manifest-filesize-approx";
					std::wstring cmd {L" --no-warnings" + cookies + fsize_approx + L" -j " + 
						(conf.output_template.empty() ? L"" : L"-o \"" + conf.output_template + L'\"') + fmt_sort + L'\"' + url + L'\"'};
					if(conf.cb_proxy && !conf.proxy.empty())
						cmd = L" --proxy " + conf.proxy + cmd;
					bottom.cmdinfo = conf.ytdlp_path.filename().wstring() + cmd;
					media_info = util::run_piped_process(L'\"' + conf.ytdlp_path.wstring() + L'\"' + cmd, &bottom.working_info);
					auto pos {media_info.rfind('}')};
					if(pos != -1)
						media_info.erase(pos + 1);
					if(bottom.working_info)
					{
						auto pos {media_info.find('{')};
						if(pos != -1)
						{
							media_info.erase(0, pos);
							try { bottom.vidinfo = nlohmann::json::parse(media_info); }
							catch(nlohmann::detail::exception e)
							{
								bottom.vidinfo.clear();
								if(lbq.item_from_value(url) != lbq.at(0).end())
									json_error(e);
							}
						}
					}
				}
				if(bottom.working_info)
				{
					if(!media_info.empty())
					{
						if(media_info[0] == '{')
						{
							if(bottom.is_ytplaylist || bottom.is_bcplaylist || bottom.is_scplaylist)
							{
								if(!bottom.playlist_info.empty())
								{
									auto playlist_size {bottom.playlist_info["entries"].size()};
									if(bottom.playsel_string.size())
										bottom.apply_playsel_string();
									else bottom.playlist_selection.assign(playlist_size, true);
									if(bottom.is_bcplaylist)
										media_website = "bandcamp.com";
									else media_website = bottom.playlist_info["webpage_url_domain"];
									if(bottom.is_yttab)
										media_title = "[channel tab] " + std::string {bottom.playlist_info["title"]};
									else media_title = "[playlist] " + std::string {bottom.playlist_info["title"]};
									if(vidsel_item.m && lbq.item_from_value(url).selected())
									{
										auto &m {*vidsel_item.m};
										auto pos {vidsel_item.pos};
										auto str {std::to_string(playlist_size)};
										if(bottom.playsel_string.empty())
											m.text(pos, (bottom.is_ytplaylist ? "Select videos (" : "Select songs (") + str + '/' + str + ")");
										else m.text(pos, (bottom.is_ytplaylist ? "Select videos (" : "Select songs (") + std::to_string(bottom.playlist_selected()) + '/' + str + ")");
										m.enabled(pos, true);
										m.enabled(pos + 1, true);
										m.enabled(pos + 2, true);
										m.enabled(pos + 3, true);
										api::refresh_window(m.handle());
										vidsel_item.m = nullptr;
									}
									for(const auto &entry : bottom.playlist_info["entries"])
									{
										if(entry.contains("id") && entry["id"] != nullptr)
											outbox.set_keyword(entry["id"].get<std::string>() + ':', "id");
									}
								}
							}
							else if(!bottom.vidinfo.empty())
							{
								if(bottom.is_bclink)
									media_website = "bandcamp.com";
								else if(bottom.vidinfo_contains("webpage_url_domain"))
									media_website = bottom.vidinfo["webpage_url_domain"];
								if(bottom.vidinfo_contains("title"))
									media_title = bottom.vidinfo["title"];
								if(bottom.vidinfo_contains("is_live") && bottom.vidinfo["is_live"] ||
									bottom.vidinfo_contains("live_status") && bottom.vidinfo["live_status"] == "is_live")
									media_title = "[live] " + media_title;
								if(bottom.vidinfo_contains("format_id"))
									format_id = bottom.vidinfo["format_id"];
								if(bottom.vidinfo_contains("resolution"))
									format_note = bottom.vidinfo["resolution"];
								else if(bottom.vidinfo_contains("format_note"))
									format_note = bottom.vidinfo["format_note"];
								if(bottom.vidinfo_contains("ext"))
									ext = bottom.vidinfo["ext"];
								if(bottom.vidinfo_contains("filesize"))
								{
									auto fsize {bottom.vidinfo["filesize"].get<std::uint64_t>()};
									filesize = util::int_to_filesize(fsize, false);
								}
								else if(bottom.vidinfo_contains("filesize_approx"))
								{
									auto fsize {bottom.vidinfo["filesize_approx"].get<std::uint64_t>()};
									if(bottom.vidinfo_contains("requested_formats"))
									{
										auto &reqfmt {bottom.vidinfo["requested_formats"]};
										if(reqfmt.size() == 2)
										{
											if(reqfmt[0].contains("filesize") && reqfmt[1].contains("filesize"))
												filesize = '~' + util::int_to_filesize(fsize, false);
											else filesize = "---";
										}
									}
									else filesize = '~' + util::int_to_filesize(fsize, false);
								}

								if(bottom.vidinfo_contains("formats"))
									bottom.show_btnfmt(true);
							}
							bottom.media_title = media_title;
							if(!is_utf8(media_title))
							{
								std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> u16conv;
								auto u16str {u16conv.from_bytes(media_title)};
								std::wstring wstr(u16str.size(), L'\0');
								memcpy(&wstr.front(), &u16str.front(), wstr.size() * 2);
								std::wstring_convert<std::codecvt_utf8<wchar_t>> u8conv;
								media_title = u8conv.to_bytes(wstr);
							}
							lbq.item_from_value(url).text(1, media_website);
							lbq.item_from_value(url).text(2, media_title);
							lbq.item_from_value(url).text(4, format_id);
							lbq.item_from_value(url).text(5, format_note);
							lbq.item_from_value(url).text(6, ext);
							lbq.item_from_value(url).text(7, filesize);

							if(bottom.vidinfo_contains("id"))
							{
								std::string idstr {bottom.vidinfo["id"]};
								outbox.set_keyword(idstr + ':', "id");
							}
						}
						else
						{
							auto stridx {lbq.item_from_value(url).text(0)};
							lbq.item_from_value(url).text(1, "");
							lbq.item_from_value(url).text(2, "yt-dlp failed to get info (see output)");
							lbq.item_from_value(url).text(3, "error");
							lbq.item_from_value(url).text(4, "");
							lbq.item_from_value(url).text(5, "");
							lbq.item_from_value(url).text(6, "");
							lbq.item_from_value(url).text(7, "");

							auto cmdline {bottom.playlist_vid_cmdinfo.empty() ? to_utf8(bottom.cmdinfo) : to_utf8(bottom.playlist_vid_cmdinfo)};
							outbox.caption("[GUI] got error executing command line: " + cmdline + "\n\n" + media_info + "\n", url);
							if(outbox.current() == url)
							{
								auto ca {outbox.colored_area_access()};
								ca->clear();
								auto p {ca->get(0)};
								p->fgcolor = ::widgets::theme::is_dark() ? ::widgets::theme::path_link_fg : color {"#569"};
							}
							if(!queue_panel.visible() && overlay.visible())
							{
								outbox.widget::show();
								overlay.hide();
							}
							if(vidsel_item.m && lbq.item_from_value(url).selected())
							{
								auto &m {*vidsel_item.m};
								for(int n {0}; n < m.size(); n++)
								{
									if(!m.enabled(n))
										m.erase(n--);
								}
								for(int n {0}; n < m.size(); n++)
								{
									if(m.text(n).empty() && n < m.size() - 1 && m.text(n + 1).empty())
									{
										m.erase(n);
										break;
									}
								}
								api::refresh_window(m.handle());
								vidsel_item.m = nullptr;
							}
						}
					}
					else
					{
						lbq.item_from_value(url).text(2, "yt-dlp did not provide any data for this URL!");
					}
				}
				if(vidsel_item.m && lbq.item_from_value(url).selected())
				{
					auto &m {*vidsel_item.m};
					for(int n {0}; n < m.size(); n++)
					{
						m.enabled(n, true);
						if(m.text(n) == "Select formats" && !bottom.btnfmt_visible())
							m.erase(n--);
					}
					api::refresh_window(m.handle());
					vidsel_item.m = nullptr;
				}
			}
			if(--active_threads == 0 && saveq)
				save_queue = true;
			if(bottom.working_info && bottom.info_thread.joinable())
				bottom.info_thread.detach();
		});
	}
}


void GUI::make_form()
{
	using widgets::theme;
	using namespace nana;
	queue_make_listbox();

	div("vert margin=20 <Top> <weight=20> <Bottom weight=325>");
	auto &plc {get_place()};
	plc["Top"].fasten(queue_panel).fasten(outbox).fasten(overlay);
	auto &bottom {bottoms.add(L"", true)};
	bottom.btndl.enabled(false);
	auto &tbpipe {outbox};
	auto &tbpipe_overlay {overlay};
	overlay.events().dbl_click([&] { show_queue(); queue_panel.focus(); });
	btn_settings.tooltip("Ctrl+S");

	plc_queue.div(R"(
		vert <lbq> <weight=20> 
		<weight=25 <btn_settings weight=106> <weight=15> <l_url> <weight=15> <btn_qact weight=126>>
	)");
	plc_queue["lbq"] << lbq;
	plc_queue["l_url"] << l_url;
	plc_queue["btn_qact"] << btn_qact;
	plc_queue["btn_settings"] << btn_settings;

	apply_theme(::widgets::theme::is_dark());

	const auto dpi {API::screen_dpi(true)};
	if(dpi == 288)
		btn_settings.image(arr_config48_png, sizeof arr_config48_png);
	else if(dpi >= 192)
		btn_settings.image(arr_config32_png, sizeof arr_config32_png);
	else if(dpi > 96)
		btn_settings.image(arr_config22_png, sizeof arr_config22_png);
	else btn_settings.image(arr_config16_png, sizeof arr_config16_png);
	btn_settings.events().click([this] { fm_settings(); });
	btn_qact.tooltip("Pops up a menu with actions that can be performed on\nthe queue items (same as right-clicking on the queue).");

	l_url.events().mouse_enter([this]
	{
		auto text {util::get_clipboard_text()};
		if(text.find(LR"(youtube.com/watch?v=)") != -1)
		{
			auto pos {text.find(L"&list=")};
			if(pos != -1)
				text.erase(pos);
		}
		
		if(text.empty())
			l_url.caption("the clipboard does not contain any text");
		else if(text.find('\n') != -1)
			l_url.caption("* multiple lines of text, make sure they're URLs *");
		else 
		{
			auto item {lbq.item_from_value(text)};
			if(item == lbq.at(0).end())
			{
				if(text.size() > 300)
					text.erase(0, text.size() - 300);
				qurl = text;
				l_url.update_caption();
			}
			else
			{
				qurl = text + L" (queue item #" + to_wstring(item.text(0)) + L")";
				l_url.update_caption();
			}
		}
		if(theme::is_dark())
			l_url.fgcolor(theme::path_link_fg.blend(colors::black, .25));
		else l_url.fgcolor(theme::path_link_fg.blend(colors::white, .25));			
	});

	l_url.events().mouse_leave([this]
	{
		auto sel {lbq.selected()};
		if(sel.empty())
			qurl.clear();
		else qurl = lbq.at(sel.front()).value<lbqval_t>();
		l_url.update_caption();
		if(theme::is_dark())
			l_url.fgcolor(theme::path_link_fg);
		else l_url.fgcolor(theme::path_link_fg);
	});

	l_url.events().mouse_up([this]
	{
		auto text {util::get_clipboard_text()};
		if(text.find('\n') != -1)
		{
			std::wstring line;
			std::wstringstream ss;
			ss.str(text);
			while(std::getline(ss, line))
			{
				if(line.back() == '\r')
					line.pop_back();
				if(!line.empty()) add_url(line);
			}
		}
		else if(!text.empty())
		{
			if(text.starts_with(LR"(https://www.youtube.com/watch?v=)"))
				if(text.find(L"&list=") == 43)
					text.erase(43);
			auto item {lbq.item_from_value(text)};
			if(item == lbq.at(0).end())
			{
				l_url.update_caption();
				add_url(text);
			}
			else l_url.caption("The URL in the clipboard is already added (queue item #" + item.text(0) + ").");

			if(theme::is_dark())
				l_url.fgcolor(theme::path_link_fg);
			else l_url.fgcolor(theme::path_link_fg);
		}
	});

	btn_qact.events().click([this]
	{
		auto url {queue_pop_menu(btn_qact.pos().x, btn_qact.pos().y + btn_qact.size().height)};
		if(!url.empty())
		{
			lbq.auto_draw(false);
			queue_remove_item(url);
			lbq.auto_draw(true);
		}
	});

	plc.collocate();
}


bool GUI::apply_theme(bool dark)
{
	using widgets::theme;

	if(dark) 
	{
		if(conf.cb_custom_dark_theme)
		{
			conf.theme_dark.contrast(conf.contrast, true);
			theme::make_dark(conf.theme_dark);
		}
		else
		{
			theme_t def {true};
			def.contrast(conf.contrast, true);
			theme::make_dark(def);
		}
	}
	else 
	{
		if(conf.cb_custom_light_theme)
		{
			conf.theme_light.contrast(conf.contrast, false);
			theme::make_light(conf.theme_light);
		}
		else
		{
			theme_t def;
			def.contrast(conf.contrast, false);
			theme::make_light(def);
		}
	}

	const auto &text {outbox.current_buffer()};
	if(!text.empty())
	{
		auto ca {outbox.colored_area_access()};
		ca->clear();
		auto p {ca->get(0)};
		if(text.starts_with("[GUI]"))
		{
			p->count = 1;
			p->fgcolor = theme::is_dark() ? theme::path_link_fg : nana::color {"#569"};
		}
		if(text.rfind("[GUI] ") != 0)
		{
			p = ca->get(outbox.text_line_count() - 2);
			p->count = 1;
			if(text.rfind("WM_CLOSE") == -1)
				p->fgcolor = widgets::theme::is_dark() ? widgets::theme::path_link_fg : nana::color {"#569"};
			else p->fgcolor = widgets::theme::is_dark() ? nana::color {"#f99"} : nana::color {"#832"};
		}
		nana::API::refresh_window(outbox);
	}

	bgcolor(theme::fmbg);
	refresh_widgets();

	for(auto &bot : bottoms)
		bot.second->bgcolor(theme::fmbg);

	return false; // don't refresh widgets a second time, already done once in this function
}


void GUI::show_queue(bool freeze_redraw)
{
	if(freeze_redraw)
		SendMessageA(hwnd, WM_SETREDRAW, FALSE, 0);
	const auto px {nana::API::screen_dpi(true) >= 144};
	change_field_attr("Bottom", "weight", 298 - 240 * bottoms.current().expcol.collapsed() - px);
	for(auto &bot : bottoms)
	{
		auto &plc {bot.second->plc};
		bot.second->btnq.caption("Show output");
		plc.field_display("prog", false);
		plc.field_display("separator", true);
		plc.collocate();
	}
	outbox.highlight(false);
	outbox.hide();
	overlay.hide();
	queue_panel.show();
	if(freeze_redraw)
		SendMessageA(hwnd, WM_SETREDRAW, TRUE, 0);
	nana::api::refresh_window(*this);
}


void GUI::show_output()
{
	SendMessageA(hwnd, WM_SETREDRAW, FALSE, 0);
	change_field_attr("Bottom", "weight", 325 - 240 * bottoms.current().expcol.collapsed());
	for(auto &bot : bottoms)
		bot.second->btnq.caption("Show queue");
	auto &curbot {bottoms.current()};
	auto &plc {curbot.plc};
	plc.field_display("prog", true);
	plc.field_display("separator", false);
	plc.collocate();
	queue_panel.hide();
	outbox.show(curbot.url);
	outbox.highlight(conf.kwhilite);
	SendMessageA(hwnd, WM_SETREDRAW, TRUE, 0);
}


void GUI::get_releases(nana::window parent_for_msgbox)
{
	thr_releases = std::thread {[parent_for_msgbox, this]
	{
		using json = nlohmann::json;
		auto jtext {util::get_inet_res("https://api.github.com/repos/ErrorFlynn/ytdlp-interface/releases", &inet_error)};
		if(!jtext.empty())
		{
			try { releases = json::parse(jtext); }
			catch(nlohmann::detail::exception e)
			{
				releases.clear();
				tmsg_parent = parent_for_msgbox;
				tmsg_title = "ytdlp-interface JSON error";
				std::string str;
				if(jtext.size() > 600)
					str = " (showing the first 600 characters)";
				tmsg_text = "Got an unexpected response when checking GitHub for a new version" + str + ":\n\n" + jtext.substr(0, 600) +
					"\n\nError from the JSON parser:\n\n" + e.what();				
				thr_releases.detach();
				return;
			}
			if(releases.is_array())
			{
				if(releases[0].contains("tag_name"))
				{					
					std::string tag_name {releases[0]["tag_name"]};
					if(tag_name.size() < 4)
					{
						releases.clear();
						tmsg_parent = parent_for_msgbox;
						tmsg_title = "ytdlp-interface error";
						tmsg_text = std::string {"Got an unexpected response when checking GitHub for a new version. "} +
							"The response is valid JSON and is properly formatted, but the value of the key \"tag_name\" " +
							"for the latest release is less that 4 characters in length: \"" + tag_name + "\"";						
					}
					else if(is_tag_a_new_version(tag_name) && conf.get_releases_at_startup)
						caption(title + "   (" + tag_name + " is available)");
				}
				else
				{
					tmsg_parent = parent_for_msgbox;
					tmsg_title = "ytdlp-interface error";
					std::string str;
					if(jtext.size() > 600)
					{
						jtext.erase(600);
						str = " (showing the first 600 characters)";
					}
					tmsg_text = "Got an unexpected response when checking GitHub for a new version" + str + ":\n\n" + jtext +
						"\n\nThe response is valid JSON and is formatted as an array (as expected), but the first element " +
						"does not contain the key \"tag_name\".";
					releases.clear();
				}
			}
			else
			{
				tmsg_parent = parent_for_msgbox;
				tmsg_title = "ytdlp-interface error";
				std::string str;
				if(jtext.size() > 600)
				{
					jtext.erase(600);
					str = " (showing the first 600 characters)";
				}
				tmsg_text = "Got an unexpected response when checking GitHub for a new version" + str + ":\n\n" + jtext +
					"\n\nThe response is valid JSON, but is not formatted as an array.";
				releases.clear();
			}
		}
		thr_releases.detach();
	}};
}


void GUI::get_latest_ffmpeg(nana::window parent_for_msgbox)
{
	using json = nlohmann::json;

	thr_releases_ffmpeg = std::thread {[=]
	{
		auto jtext {util::get_inet_res("https://api.github.com/repos/yt-dlp/FFmpeg-Builds/releases", &inet_error)};
		if(!jtext.empty())
		{
			json json_ffmpeg;
			try { json_ffmpeg = json::parse(jtext); }
			catch(nlohmann::detail::exception e)
			{
				tmsg_parent = parent_for_msgbox;
				tmsg_title = "ytdlp-interface error";
				std::string str;
				if(jtext.size() > 600)
				{
					jtext.erase(600);
					str = " (showing the first 600 characters)";
				}
				tmsg_text = "Got an unexpected response when checking GitHub for a new FFmpeg version" + str + ":\n\n" + jtext +
					"\n\nError from the JSON parser:\n\n" + e.what();
				if(thr_releases_ffmpeg.joinable())
					thr_releases_ffmpeg.detach();
				return;
			}
			if(!json_ffmpeg.empty() && json_ffmpeg.is_array() && json_ffmpeg[0].contains("assets"))
			{
				for(auto &el : json_ffmpeg[0]["assets"])
				{
					std::string url {el["browser_download_url"]};
					if(url.find(X64 ? "win64-gpl.zip" : "win32-gpl.zip") != -1)
					{
						url_latest_ffmpeg = url;
						size_latest_ffmpeg = el["size"];
						std::string date {json_ffmpeg[0]["published_at"]};
						ver_ffmpeg_latest.year = stoi(date.substr(0, 4));
						ver_ffmpeg_latest.month = stoi(date.substr(5, 2));
						ver_ffmpeg_latest.day = stoi(date.substr(8, 2));
						break;
					}
				}
			}
		}
		if(thr_releases_ffmpeg.joinable())
			thr_releases_ffmpeg.detach();
	}};
}


void GUI::get_latest_ytdlp(nana::window parent_for_msgbox)
{
	using json = nlohmann::json;

	thr_releases_ytdlp = std::thread {[=]
	{
		std::string jtext;
		auto fname {conf.ytdlp_path.filename().string()};
		if(fname.empty())
			fname = ytdlp_fname;
		if(fname == "yt-dlp.exe" || fname == "yt-dlp_x86.exe")
		{
			if(conf.ytdlp_nightly)
				jtext = util::get_inet_res("https://api.github.com/repos/yt-dlp/yt-dlp-nightly-builds/releases/latest", &inet_error);
			else jtext = util::get_inet_res("https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest", &inet_error);
		}
		else jtext = util::get_inet_res("https://api.github.com/repos/ytdl-patched/ytdl-patched/releases/latest", &inet_error);
		if(!jtext.empty() && thr_releases_ytdlp.joinable())
		{
			json json_ytdlp;
			try { json_ytdlp = json::parse(jtext); }
			catch(nlohmann::detail::exception e)
			{
				tmsg_parent = parent_for_msgbox;
				tmsg_title = "ytdlp-interface error";
				std::string str;
				if(jtext.size() > 600)
				{
					jtext.erase(600);
					str = " (showing the first 600 characters)";
				}
				tmsg_text = "Got an unexpected response when checking GitHub for a new yt-dlp version" + str + ":\n\n" + jtext +
					"\n\nError from the JSON parser:\n\n" + e.what();
				if(thr_releases_ytdlp.joinable())
					thr_releases_ytdlp.detach();
				return;
			}
			if(!json_ytdlp.empty() && thr_releases_ytdlp.joinable())
			{
				for(auto &el : json_ytdlp["assets"])
				{
					std::string url {el["browser_download_url"]};
					if(url.find(fname) != -1)
					{
						url_latest_ytdlp = url;
						size_latest_ytdlp = el["size"];
						url_latest_ytdlp_relnotes = json_ytdlp["html_url"];
						std::string date {json_ytdlp["published_at"]};
						ver_ytdlp_latest.year = stoi(date.substr(0, 4));
						ver_ytdlp_latest.month = stoi(date.substr(5, 2));
						ver_ytdlp_latest.day = stoi(date.substr(8, 2));
						break;
					}
				}
			}
		}
		if(thr_releases_ytdlp.joinable())
			thr_releases_ytdlp.detach();
	}};
}


void GUI::get_versions()
{
	thr_versions = std::thread {[this]
	{
		get_version_ffmpeg(false);
		get_version_ytdlp();
		if(thr_ver_ffmpeg.joinable())
			thr_ver_ffmpeg.join();
		if(thr_versions.joinable())
			thr_versions.detach();
	}};
}


void GUI::get_version_ytdlp()
{
	if(!conf.ytdlp_path.empty())
	{
		if(fs::exists(conf.ytdlp_path))
		{
			auto ver {util::run_piped_process(L'\"' + conf.ytdlp_path.wstring() + L"\" --version")};
			if(ver.size() >= 10)
			{
				ver_ytdlp = {-1, -1, -1};
				auto rawver {ver.substr(0, 10)};
				try { ver_ytdlp.year = stoi(rawver.substr(0, 4)); }
				catch(...) { return; }
				ver_ytdlp.month = stoi(rawver.substr(5, 2));
				ver_ytdlp.day = stoi(rawver.substr(8, 2));
			}
		}
		else conf.ytdlp_path.clear();
	}
}


void GUI::get_version_ffmpeg(bool auto_detach)
{
	if(!thr_ver_ffmpeg.joinable())
	{
		thr_ver_ffmpeg = std::thread {[=, this]
		{
			if(!conf.ffmpeg_path.empty())
			{
				std::wstring cmd {L'\"' + (conf.ffmpeg_path / "ffmpeg.exe").wstring() + L"\" -version"};
				auto ver {util::run_piped_process(cmd)};
				auto pos {ver.find("--extra-version=")};
				if(pos != -1)
				{
					auto rawver {ver.substr(pos + 16, 8)};
					ver_ffmpeg.year = stoi(rawver.substr(0, 4));
					ver_ffmpeg.month = stoi(rawver.substr(4, 2));
					ver_ffmpeg.day = stoi(rawver.substr(6, 2));
				}
			}
			if(auto_detach && thr_ver_ffmpeg.joinable())
				thr_ver_ffmpeg.detach();
		}};
	}
}


bool GUI::is_ytlink(std::wstring url)
{
	auto pos {url.find(L"www.")};
	if(pos != -1)
		url.erase(pos, 4);
	pos = url.find(L"m.youtube.");
	if(pos != -1)
		url.erase(pos, 2);
	if(url.starts_with(L"https://music.youtube.com"))
		return true;
	if(url.starts_with(L"https://youtube.com/") && url.size() > 20 || url.starts_with(L"youtube.com/") && url.size() > 12)
		return true;
	if(url.starts_with(L"https://youtu.be/") && url.size() > 17 || url.starts_with(L"youtu.be/") && url.size() > 9)
		return true;
	return false;
}


bool GUI::is_ytchan(std::wstring url)
{
	if(url.find(L"youtube.com/c/") != -1 || url.find(L"youtube.com/@") != -1 ||
		url.find(L"youtube.com/channel/") != -1 || url.find(L"youtube.com/user/") != -1)
		return true;
	auto pos {url.find(L"youtube.com/")};
	if(pos != -1 && url.size() > pos + 12 && url.find(L"watch?v=") == -1 && url.find(L"list=") == -1 && url.find(L"/live/") == -1
		&& url.find(L"music.youtube.com") == -1)
		return true;
	return false;
}


bool GUI::is_scplaylist(std::wstring url)
{
	if(url.find(L"soundcloud.com/") != -1)
	{
		auto pos_in {url.find(L"?in=")}, pos_sets {url.find(L"/sets/")};
		if(pos_sets != -1 && pos_in == -1)
			return true;
	}
	return false;
}


void GUI::taskbar_overall_progress()
{
	if(i_taskbar && lbq.at(0).size() > 1)
	{
		ULONGLONG completed {0}, total {lbq.at(0).size()};
		for(auto &item : lbq.at(0))
			if(item.text(3) == "done")
				completed++;
		if(completed)
			if(completed == total)
				i_taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
			else i_taskbar->SetProgressValue(hwnd, completed, total);
		else i_taskbar->SetProgressValue(hwnd, 1, -1);
	}
}


void GUI::on_btn_dl(std::wstring url)
{
	use_ffmpeg_location = !ffmpeg_location_in_conf_file();
	taskbar_overall_progress();
	if(!process_queue_item(url))
		return; // the queue item was stopped, not started

	auto items_currently_downloading {0};
	
	for(auto &bottom : bottoms)
		if(bottom.second->started())
			items_currently_downloading++;

	auto item_total {lbq.at(0).size()};
	if(item_total > 1)
	{
		auto pos {std::stoi(lbq.item_from_value(url).text(0)) - 1};
		while(++pos < item_total && items_currently_downloading < conf.max_concurrent_downloads)
		{
			auto next_item {lbq.at(0).at(pos)};
			std::wstring next_url {next_item.value<lbqval_t>()};
			auto text {next_item.text(3)};
			if(text == "queued" || text.find("stopped") == 0)
			{
				process_queue_item(next_url);
				items_currently_downloading++;
			}
		}
	}
}


std::wstring GUI::next_startable_url(std::wstring current_url)
{
	if(autostart_next_item)
	{
		auto item_total {lbq.at(0).size()};
		if(item_total > 1)
		{
			auto items_currently_downloading {0};
			if(current_url == L"current")
				current_url = bottoms.current().url;
			for(auto item : lbq.at(0))
			{
				const auto text {item.text(3)};
				if(text == "downloading" || (text.find('%') != -1 && text.find("stopped") == -1) || text == "started")
					items_currently_downloading++;
			}

			auto pos {current_url.empty() ? -1 : stoi(lbq.item_from_value(current_url).text(0)) - 1};
			if(pos == item_total - 1)
				pos = -1;
			while(++pos < item_total && items_currently_downloading < conf.max_concurrent_downloads)
			{
				auto next_item {lbq.at(0).at(pos)};
				const auto &next_url {next_item.value<lbqval_t>().url};
				auto text {next_item.text(3)};
				if(text == "queued" || text.starts_with("stopped"))
					return next_url;
			}
		}
	}
	return L"";
}


bool GUI::lbq_has_scrollbar()
{
	nana::paint::graphics g {{100, 100}};
	g.typeface(lbq.typeface());
	const auto text_height {g.text_extent_size("mm").height};
	const auto item_height {text_height + lbq.scheme().item_height_ex};
	if(lbq.at(0).size() * item_height > lbq.size().height - 25)
		return true;
	return false;
}


void GUI::adjust_lbq_headers()
{
	using namespace util;

	auto zero {scale(30)},
		one {scale(20 + !conf.col_site_text * 10) * conf.col_site_icon + scale(110) * conf.col_site_text},
		three {scale(116)},
		four {scale(130) * lbq.column_at(4).visible() + scale(16) * lbq_has_scrollbar()},
		five {scale(150) * lbq.column_at(5).visible()},
		six {scale(60) * lbq.column_at(6).visible()},
		seven {scale(100) * lbq.column_at(7).visible()};

	if(conf.col_site_icon || conf.col_site_text)
	{
		lbq.column_at(1).width(one);
		lbq.column_at(1).text(conf.col_site_text ? "Website" : "");
		if(!lbq.column_at(1).visible())
			lbq.column_at(1).visible(true);
	}
	else lbq.column_at(1).visible(false);

	const auto two_computed {lbq.size().width - (zero + one + three + four + five + six + seven) - scale(9)};
	lbq.column_at(2).width(two_computed);
}


bool GUI::ffmpeg_location_in_conf_file()
{
	std::ifstream infile {conf.ytdlp_path.parent_path() / "yt-dlp.conf"};
	if(infile)
	{
		std::stringstream ss;
		ss << infile.rdbuf();
		if(ss.str().find("--ffmpeg-location") != -1)
			return true;
	}
	return false;
}


HRESULT STDMETHODCALLTYPE GUI::DragEnter(IDataObject *pDataObj, DWORD, POINTL, DWORD*)
{
	FORMATETC fmtetc {CF_TEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
	STGMEDIUM stgmedium;
	HRESULT hr {pDataObj->GetData(&fmtetc, &stgmedium)};
	if(FAILED(hr)) return hr;

	auto ptext {reinterpret_cast<const char *>(GlobalLock(stgmedium.hGlobal))};
	if(ptext)
	{
		std::string text {ptext};
		if(text.starts_with("http://") || text.starts_with("https://"))
		{
			drop_cliptext_temp = util::get_clipboard_text();
			util::set_clipboard_text(hwnd, nana::to_wstring(text));
			l_url.events().mouse_enter.emit({}, l_url);
			ReleaseStgMedium(&stgmedium);
			return S_OK;
		}
	}
	ReleaseStgMedium(&stgmedium);
	return S_FALSE;
}


HRESULT STDMETHODCALLTYPE GUI::DragLeave()
{
	l_url.events().mouse_leave.emit({}, l_url);
	util::set_clipboard_text(hwnd, drop_cliptext_temp);
	return S_OK;
}


HRESULT STDMETHODCALLTYPE GUI::Drop(IDataObject *pDataObj, DWORD, POINTL, DWORD*)
{
	l_url.events().mouse_up.emit({}, l_url);
	util::set_clipboard_text(hwnd, drop_cliptext_temp);
	return S_OK;
}