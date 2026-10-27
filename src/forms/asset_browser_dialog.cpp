#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include "paths.hpp"
#include <condition_variable>
#include <mutex>
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
struct AssetBrowserRequest {
    std::string path, query;
    AssetFilter filter = AssetFilter::All;
    std::uint64_t generation = 0;
    bool scan = true;
    std::atomic<bool> cancel{false};
};
struct AssetBrowserResult {
    std::uint64_t generation = 0;
    bool scan = true;
    AssetListing listing;
    std::shared_ptr<const AssetPreview> preview;
    std::string error;
};
struct AssetBrowserWorker {
    std::mutex mutex;
    std::condition_variable ready;
    std::shared_ptr<AssetBrowserRequest> pending, current;
    bool stopping = false;
    std::thread thread;
};
namespace {
struct AssetBrowserDelivery {
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<const AssetBrowserResult> result;
    void operator()() const {
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_asset_browser(result);
        }
    }
};
struct AssetBrowserWork {
    std::shared_ptr<AssetBrowserWorker> state;
    std::weak_ptr<EditorDialog> owner;
    EditorDialog* dispatcher;
    void operator()() const {
        for (;;) {
            std::shared_ptr<AssetBrowserRequest> request;
            {
                std::unique_lock<std::mutex> lock((*state).mutex);
                while (!(*state).stopping && !(*state).pending) {
                    (*state).ready.wait(lock);
                }
                if ((*state).stopping) {
                    return;
                }
                request = std::move((*state).pending);
                (*state).current = request;
            }
            std::shared_ptr<AssetBrowserResult> result = std::make_shared<AssetBrowserResult>();
            (*result).generation = (*request).generation;
            (*result).scan = (*request).scan;
            try {
                if ((*request).scan) {
                    (*result).listing = scan_assets((*request).path, (*request).query, (*request).filter,
                                                    8192, &(*request).cancel);
                } else if (!(*request).cancel.load()) {
                    (*result).preview =
                        std::make_shared<const AssetPreview>(load_asset_preview((*request).path));
                }
            } catch (const std::exception& exception) {
                (*result).error = exception.what();
            }
            if (!(*request).cancel.load()) {
                static_cast<void>((*dispatcher).begin_invoke(AssetBrowserDelivery{owner, result}));
            }
            {
                std::lock_guard<std::mutex> lock((*state).mutex);
                (*state).current.reset();
            }
        }
    }
};
} // namespace
void EditorDialog::initialize_asset_browser() {
    panel_ = {0, 0, 780, 500};
    const std::shared_ptr<Editor> editor = editor_.lock();
    std::string directory = (*editor).asset_directory;
    if (directory.empty()) {
        directory =
            (*editor).document.filename.empty()
                ? path_to_utf8(std::filesystem::current_path())
                : path_to_utf8(
                      std::filesystem::absolute(path_from_utf8((*editor).document.filename)).parent_path());
    }
    label("asset-folder-label", tr("Folder"), {20, 48, 65, 28});
    asset_location_ = gf::make_control<gf::TextBox>(gf::StableId("asset-location"));
    (*asset_location_).set_text(directory);
    put(asset_location_, {90, 48, 440, 28});
    label("asset-search-label", tr("Search"), {20, 88, 65, 28});
    asset_search_ = gf::make_control<gf::TextBox>(gf::StableId("asset-search"));
    put(asset_search_, {90, 88, 270, 28});
    asset_filter_ = gf::make_control<gf::ComboBox>(gf::StableId("asset-filter"));
    (*asset_filter_).set_accessible_name(tr("Asset type"));
    (*asset_filter_).set_items({tr("All images"), "ICO", "CUR", tr("Bitmap images"), "SVG"});
    (*asset_filter_).set_selected_index(0);
    put(asset_filter_, {380, 88, 160, 28});
    label("asset-bookmark-label", tr("Bookmarks"), {20, 128, 85, 28});
    asset_bookmark_ = gf::make_control<gf::ComboBox>(gf::StableId("asset-bookmark"));
    put(asset_bookmark_, {110, 128, 350, 28});
    asset_list_ = gf::make_control<gf::ListBox>(gf::StableId("asset-list"));
    (*asset_list_).set_accessible_name(tr("Assets in folder"));
    (*asset_list_).set_item_height(30);
    put(asset_list_, {20, 170, 360, 220});
    asset_canvas_ = gf::make_control<gf::RasterCanvas>(gf::StableId("asset-preview"));
    (*asset_canvas_).set_accessible_name(tr("Asset preview"));
    (*asset_canvas_).set_zoom(1);
    (*asset_canvas_).set_view_origin({0, 0});
    put(asset_canvas_, {400, 170, 360, 150});
    asset_representations_ = gf::make_control<gf::ComboBox>(gf::StableId("asset-representations"));
    (*asset_representations_).set_accessible_name(tr("Images"));
    put(asset_representations_, {400, 326, 360, 28});
    asset_details_ = gf::make_control<gf::TextBox>(gf::StableId("asset-details"));
    (*asset_details_).set_accessible_name(tr("Image properties"));
    (*asset_details_).set_read_only(true);
    (*asset_details_).set_multiline(true);
    (*asset_details_).set_auto_scroll(true);
    put(asset_details_, {400, 360, 360, 40});
    asset_status_ = gf::make_control<gf::Label>(gf::StableId("asset-status"));
    put(asset_status_, {20, 404, 740, 22});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    put(error_, {20, 426, 740, 22});
    std::vector<std::shared_ptr<gf::Button>> buttons;
    buttons.push_back(button("asset-folder", tr("Choose folder…"), {545, 48, 145, 28}));
    buttons.push_back(button("asset-up", tr("Up"), {700, 48, 60, 28}));
    buttons.push_back(button("asset-refresh", tr("Search / refresh"), {550, 88, 210, 28}));
    buttons.push_back(button("asset-bookmark-add", tr("Bookmark folder"), {475, 128, 150, 28}));
    buttons.push_back(button("asset-bookmark-remove", tr("Remove bookmark"), {635, 128, 125, 28}));
    asset_open_ = button("asset-open", tr("Open"), {20, 460, 80, 28});
    asset_stamp_ = button("asset-stamp", tr("Use as stamp"), {108, 460, 135, 28});
    asset_copy_ = button("asset-copy", tr("Copy path"), {251, 460, 100, 28});
    buttons.insert(buttons.end(), {asset_open_, asset_stamp_, asset_copy_});
    asset_batch_ = button("asset-batch", tr("Add to batch"), {359, 460, 120, 28});
    buttons.push_back(asset_batch_);
    asset_reveal_ = button("asset-reveal", tr("Reveal in file manager"), {487, 460, 175, 28});
    buttons.push_back(asset_reveal_);
    button("dialog-cancel", tr("Close"), {670, 460, 90, 28});
    for (const std::shared_ptr<gf::Button>& control : buttons) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::asset_clicked>(*this)));
    }
    subscriptions_.push_back(
        (*asset_filter_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::asset_filter_changed>(*this)));
    subscriptions_.push_back(
        (*asset_representations_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::asset_representation_changed>(*this)));
    subscriptions_.push_back(
        (*asset_bookmark_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::asset_bookmark_changed>(*this)));
    subscriptions_.push_back((*asset_list_)
                                 .selection_changed()
                                 .subscribe(*this, gf::Delegate<const gf::ListSelectionChange&>::bind<
                                                       EditorDialog, &EditorDialog::asset_selected>(*this)));
    subscriptions_.push_back(
        (*asset_list_)
            .item_activated()
            .subscribe(*this,
                       gf::Delegate<std::size_t>::bind<EditorDialog, &EditorDialog::asset_activated>(*this)));
    for (const std::shared_ptr<gf::TextBox>& control : {asset_location_, asset_search_}) {
        subscriptions_.push_back((*control).text_changed().subscribe(
            *this,
            gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::asset_input_changed>(*this)));
    }
    synchronizing_ = true;
    try {
        if (!(*editor).asset_bookmark_path.empty()) {
            asset_bookmarks_ = load_asset_bookmarks((*editor).asset_bookmark_path);
            (*asset_bookmark_).set_items(asset_bookmarks_);
        }
    } catch (const std::exception& exception) {
        asset_bookmarks_valid_ = false;
        (*error_).set_text(exception.what());
    }
    synchronizing_ = false;
    clear_asset_preview();
}
void EditorDialog::stop_asset_browser() {
    stop_asset_reveal();
    ++asset_generation_;
    if (!asset_worker_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock((*asset_worker_).mutex);
        (*asset_worker_).stopping = true;
        if ((*asset_worker_).current) {
            (*(*asset_worker_).current).cancel.store(true);
        }
        (*asset_worker_).pending.reset();
    }
    (*asset_worker_).ready.notify_all();
    if ((*asset_worker_).thread.joinable()) {
        (*asset_worker_).thread.join();
    }
    asset_worker_.reset();
}
void EditorDialog::clear_asset_preview() {
    asset_open_after_preview_ = false;
    asset_preview_.reset();
    (*asset_canvas_).clear_bitmap();
    const bool previous = synchronizing_;
    synchronizing_ = true;
    (*asset_representations_).set_items({});
    synchronizing_ = previous;
    (*asset_details_).set_text("");
    (*asset_open_).set_enabled(false);
    (*asset_stamp_).set_enabled(false);
    (*asset_copy_).set_enabled(false);
    (*asset_reveal_).set_enabled(false);
    (*asset_batch_).set_enabled(false);
}
void EditorDialog::request_assets(bool scan) {
    if (!attached_window()) {
        return;
    }
    std::shared_ptr<AssetBrowserRequest> request = std::make_shared<AssetBrowserRequest>();
    (*request).scan = scan;
    (*request).generation = ++asset_generation_;
    if (scan) {
        (*request).path = (*asset_location_).text();
        (*request).query = (*asset_search_).text();
        (*request).filter = static_cast<AssetFilter>((*asset_filter_).selected_index().value_or(0));
        synchronizing_ = true;
        (*asset_list_).clear_items();
        synchronizing_ = false;
        asset_listing_ = {};
    } else {
        const std::optional<std::size_t> selected = (*asset_list_).selected_index();
        if (!selected || *selected >= asset_listing_.entries.size()) {
            return;
        }
        (*request).path = asset_listing_.entries[*selected].path;
    }
    clear_asset_preview();
    (*error_).set_text("");
    (*asset_status_).set_text(scan ? tr("Reading folder…") : tr("Loading preview…"));
    if (!asset_worker_) {
        asset_worker_ = std::make_shared<AssetBrowserWorker>();
        (*asset_worker_).thread = std::thread(AssetBrowserWork{
            asset_worker_, std::static_pointer_cast<EditorDialog>(shared_from_this()), this});
    }
    {
        std::lock_guard<std::mutex> lock((*asset_worker_).mutex);
        if ((*asset_worker_).current) {
            (*(*asset_worker_).current).cancel.store(true);
        }
        (*asset_worker_).pending = request;
    }
    (*asset_worker_).ready.notify_one();
}
void EditorDialog::deliver_asset_browser(std::shared_ptr<const AssetBrowserResult> result) {
    if ((*result).generation != asset_generation_ || !attached_window()) {
        return;
    }
    try {
        if (!(*result).error.empty()) {
            (*error_).set_text((*result).error);
            (*asset_status_).set_text(tr("Could not load the selected folder or asset."));
            return;
        }
        if ((*result).scan) {
            asset_listing_ = (*result).listing;
            std::vector<std::string> labels, identities;
            for (const AssetEntry& entry : asset_listing_.entries) {
                labels.push_back((entry.kind == AssetKind::Folder ? "[+] " : "") + entry.name);
                identities.push_back(entry.path);
            }
            synchronizing_ = true;
            (*asset_list_).set_items(std::move(labels));
            (*asset_list_).set_item_stable_ids(std::move(identities));
            (*asset_list_).set_enabled(true);
            (*asset_location_).set_text(asset_listing_.directory);
            synchronizing_ = false;
            const std::shared_ptr<Editor> editor = editor_.lock();
            if (editor) {
                (*editor).asset_directory = asset_listing_.directory;
            }
            (*asset_status_)
                .set_text(std::to_string(asset_listing_.entries.size()) + " " +
                          (asset_listing_.truncated
                               ? tr("entries shown; listing limited. Narrow the search.")
                               : tr("entries. Select a file to preview; open folders to navigate.")));
        } else {
            asset_preview_ = (*result).preview;
            std::vector<std::string> labels;
            for (std::size_t index = 0; index < (*asset_preview_).images.frames.size(); ++index) {
                const IconFrame& frame = (*asset_preview_).images.frames[index];
                std::string label = "#" + std::to_string(index + 1) + "  " +
                                    std::to_string(frame.image.width) + " × " +
                                    std::to_string(frame.image.height);
                if (frame.source) {
                    label += " · " + std::to_string((*frame.source).format.bits_per_pixel) + " bpp";
                }
                labels.push_back(std::move(label));
            }
            synchronizing_ = true;
            (*asset_representations_).set_items(std::move(labels));
            (*asset_representations_).set_selected_index((*asset_preview_).preferred);
            synchronizing_ = false;
            show_asset_representation();
            (*asset_batch_).set_enabled(true);
            (*asset_open_).set_enabled(true);
            (*asset_copy_).set_enabled(true);
            (*asset_status_)
                .set_text(tr(
                    "Preview only. Open loads the file; stamp placement changes pixels only when applied."));
            if (asset_open_after_preview_) {
                asset_open_after_preview_ = false;
                asset_clicked(*asset_open_);
            }
        }
    } catch (const std::exception& exception) {
        synchronizing_ = false;
        clear_asset_preview();
        (*error_).set_text(exception.what());
    }
}
void EditorDialog::show_asset_representation() {
    if (!asset_preview_) {
        return;
    }
    const std::size_t index = (*asset_representations_).selected_index().value_or(0);
    const IconFrame& frame = (*asset_preview_).images.frames.at(index);
    publish_image(render_asset_preview(frame, 360, 150), *asset_canvas_);
    bool xor_pixels = false;
    for (std::size_t pixel = 0; pixel < frame.image.pixels.size(); ++pixel) {
        xor_pixels = xor_pixels || legacy_xor_pixel(frame, pixel);
    }
    (*asset_stamp_).set_enabled(!xor_pixels);
    std::string text = tr("Fit preview; nearest sampling, no enlargement.") + "\n" +
                       std::to_string((*asset_preview_).fingerprint.bytes) + " bytes";
    if ((*asset_preview_).images.kind == ContainerKind::Cursor) {
        text += " · " + tr("Cursor hotspot") + " " + std::to_string(frame.hotspot_x) + ", " +
                std::to_string(frame.hotspot_y);
    }
    if (xor_pixels) {
        text += " · " + tr("Legacy XOR: stamp unavailable.");
    }
    (*asset_details_).set_text(text);
}
void EditorDialog::asset_representation_changed(std::optional<std::size_t>) {
    if (!synchronizing_) {
        show_asset_representation();
    }
}
void EditorDialog::asset_filter_changed(std::optional<std::size_t>) {
    if (!synchronizing_) {
        request_assets(true);
    }
}
void EditorDialog::asset_input_changed(const std::string&) {
    if (!synchronizing_) {
        ++asset_generation_;
        clear_asset_preview();
        (*asset_list_).set_enabled(false);
        (*asset_status_).set_text(tr("Press Search / refresh to apply the folder and search."));
    }
}
void EditorDialog::asset_selected(const gf::ListSelectionChange& selection) {
    if (synchronizing_) {
        return;
    }
    ++asset_generation_;
    clear_asset_preview();
    if (selection.active_index && *selection.active_index < asset_listing_.entries.size()) {
        const AssetEntry& entry = asset_listing_.entries[*selection.active_index];
        if (entry.kind == AssetKind::Folder) {
            (*asset_open_).set_enabled(true);
            (*asset_copy_).set_enabled(true);
            (*asset_status_).set_text(entry.path);
        } else {
            request_assets(false);
            (*asset_copy_).set_enabled(true);
            (*asset_batch_).set_enabled(true);
        }
        (*asset_reveal_).set_enabled(true);
    }
}
void EditorDialog::asset_activated(std::size_t index) {
    if (index >= asset_listing_.entries.size() || (*asset_list_).selected_index() != index) {
        return;
    }
    if (asset_listing_.entries[index].kind != AssetKind::Folder && !asset_preview_) {
        request_assets(false);
        asset_open_after_preview_ = true;
        (*asset_copy_).set_enabled(true);
        (*asset_batch_).set_enabled(true);
    } else {
        asset_clicked(*asset_open_);
    }
}
void EditorDialog::asset_bookmark_changed(std::optional<std::size_t> index) {
    if (!synchronizing_ && index && *index < asset_bookmarks_.size()) {
        synchronizing_ = true;
        (*asset_location_).set_text(asset_bookmarks_[*index]);
        (*asset_search_).set_text("");
        synchronizing_ = false;
        request_assets(true);
    }
}
void EditorDialog::asset_clicked(gf::ButtonBase& control) {
    const std::shared_ptr<EditorDialog> keep_alive =
        std::static_pointer_cast<EditorDialog>(shared_from_this());
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    const std::string id(control.stable_id().value());
    try {
        (*error_).set_text("");
        if (id == "asset-refresh") {
            request_assets(true);
        } else if (id == "asset-folder" || id == "asset-up") {
            const std::string folder =
                id == "asset-folder"
                    ? (*editor).choose_asset_directory(std::string((*asset_location_).text()))
                    : path_to_utf8(path_from_utf8((*asset_location_).text()).parent_path());
            if (!folder.empty()) {
                synchronizing_ = true;
                (*asset_location_).set_text(folder);
                (*asset_search_).set_text("");
                synchronizing_ = false;
                request_assets(true);
            }
        } else if (id == "asset-bookmark-add" || id == "asset-bookmark-remove") {
            if (!asset_bookmarks_valid_ || (*editor).asset_bookmark_path.empty()) {
                throw std::runtime_error(tr("Asset bookmark storage is unavailable."));
            }
            std::vector<std::string> updated = asset_bookmarks_;
            if (id == "asset-bookmark-add") {
                if (asset_listing_.directory.empty() || !(*asset_list_).enabled()) {
                    return;
                }
                if (std::find(updated.begin(), updated.end(), asset_listing_.directory) == updated.end()) {
                    updated.push_back(asset_listing_.directory);
                }
            } else {
                const std::optional<std::size_t> selected = (*asset_bookmark_).selected_index();
                if (!selected || *selected >= updated.size()) {
                    return;
                }
                updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(*selected));
            }
            save_asset_bookmarks(updated, (*editor).asset_bookmark_path);
            asset_bookmarks_ = std::move(updated);
            synchronizing_ = true;
            (*asset_bookmark_).set_items(asset_bookmarks_);
            synchronizing_ = false;
        } else {
            const std::optional<std::size_t> selected = (*asset_list_).selected_index();
            if (!selected || *selected >= asset_listing_.entries.size() || !(*asset_list_).enabled()) {
                return;
            }
            const AssetEntry entry = asset_listing_.entries[*selected];
            if (id == "asset-reveal") {
                start_asset_reveal(entry.path);
            } else if (id == "asset-copy") {
                (*editor).copy_asset_path(entry.path);
            } else if (id == "asset-batch" && entry.kind != AssetKind::Folder) {
                if ((*editor).batch_sources.size() >= 256) {
                    throw std::runtime_error("Choose at most 256 source files.");
                }
                (*editor).batch_sources.push_back(entry.path);
                (*asset_status_)
                    .set_text(tr("Batch production") + ": " + std::to_string((*editor).batch_sources.size()));
            } else if (id == "asset-open" && entry.kind == AssetKind::Folder) {
                synchronizing_ = true;
                (*asset_location_).set_text(entry.path);
                (*asset_search_).set_text("");
                synchronizing_ = false;
                request_assets(true);
            } else if (asset_preview_ && asset_unchanged(*asset_preview_)) {
                const std::shared_ptr<const AssetPreview> preview = asset_preview_;
                if (id == "asset-open") {
                    (*editor).open_browser_asset(*preview);
                } else if (id == "asset-stamp") {
                    Image stamp =
                        asset_stamp(*preview, (*asset_representations_).selected_index().value_or(0));
                    (*editor).use_asset_stamp(std::move(stamp));
                    (*editor).close_editor_dialog();
                }
            } else if (asset_preview_) {
                clear_asset_preview();
                throw std::runtime_error(tr("Asset changed while loading. Refresh its preview."));
            }
        }
    } catch (const std::exception& exception) {
        synchronizing_ = false;
        (*error_).set_text(exception.what());
    }
}
} // namespace paint::forms
