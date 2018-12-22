//  SuperTux
//  Copyright (C) 2006 Matthias Braun <matze@braunis.de>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "object/tilemap.hpp"

#include <tuple>
#include <cmath>

#include "editor/editor.hpp"
#include "supertux/debug.hpp"
#include "supertux/globals.hpp"
#include "supertux/sector.hpp"
#include "supertux/tile.hpp"
#include "supertux/tile_set.hpp"
#include "util/reader.hpp"
#include "util/reader_mapping.hpp"
#include "util/writer.hpp"
#include "video/drawing_context.hpp"
#include "video/layer.hpp"
#include "video/surface.hpp"

TileMap::TileMap(const TileSet *new_tileset) :
  ExposedObject<TileMap, scripting::TileMap>(this),
  PathObject(),
  m_editor_active(true),
  m_tileset(new_tileset),
  m_tiles(),
  m_real_solid(false),
  m_effective_solid(false),
  m_speed_x(1),
  m_speed_y(1),
  m_width(0),
  m_height(0),
  m_z_pos(0),
  m_offset(Vector(0,0)),
  m_movement(0,0),
  m_flip(NO_FLIP),
  m_alpha(1.0),
  m_current_alpha(1.0),
  m_remaining_fade_time(0),
  m_tint(1, 1, 1),
  m_current_tint(1, 1, 1),
  m_remaining_tint_fade_time(0),
  m_running(false),
  m_draw_target(DrawingTarget::COLORMAP),
  m_new_size_x(0),
  m_new_size_y(0),
  m_new_offset_x(0),
  m_new_offset_y(0),
  m_add_path(false)
{
}

TileMap::TileMap(const TileSet *tileset_, const ReaderMapping& reader) :
  GameObject(reader),
  ExposedObject<TileMap, scripting::TileMap>(this),
  PathObject(),
  m_editor_active(true),
  m_tileset(tileset_),
  m_tiles(),
  m_real_solid(false),
  m_effective_solid(false),
  m_speed_x(1),
  m_speed_y(1),
  m_width(-1),
  m_height(-1),
  m_z_pos(0),
  m_offset(Vector(0,0)),
  m_movement(Vector(0,0)),
  m_flip(NO_FLIP),
  m_alpha(1.0),
  m_current_alpha(1.0),
  m_remaining_fade_time(0),
  m_tint(1, 1, 1),
  m_current_tint(1, 1, 1),
  m_remaining_tint_fade_time(0),
  m_running(false),
  m_draw_target(DrawingTarget::COLORMAP),
  m_new_size_x(0),
  m_new_size_y(0),
  m_new_offset_x(0),
  m_new_offset_y(0),
  m_add_path(false)
{
  assert(m_tileset);

  reader.get("solid",  m_real_solid);

  bool backward_compatibility_fudge = false;

  if (!reader.get("speed-x", m_speed_x)) {
    if (reader.get("speed",  m_speed_x)) {
      backward_compatibility_fudge = true;
    }
  }

  if (!reader.get("speed-y", m_speed_y)) {
    if (backward_compatibility_fudge) {
      m_speed_y = m_speed_x;
    }
  }

  m_z_pos = reader_get_layer (reader, /* default = */ 0);

  if (!Editor::is_active()) {
    if (m_real_solid && ((m_speed_x != 1) || (m_speed_y != 1))) {
      log_warning << "Speed of solid tilemap is not 1. fixing" << std::endl;
      m_speed_x = 1;
      m_speed_y = 1;
    }
  }

  init_path(reader, false);

  std::string draw_target_s = "normal";
  reader.get("draw-target", draw_target_s);
  if (draw_target_s == "normal") m_draw_target = DrawingTarget::COLORMAP;
  if (draw_target_s == "lightmap") m_draw_target = DrawingTarget::LIGHTMAP;

  if (reader.get("alpha", m_alpha)) {
    m_current_alpha = m_alpha;
  }

  std::vector<float> vColor;
  if (reader.get("tint", vColor)) {
    m_current_tint = Color(vColor);
    m_tint = m_current_tint;
  }

  /* Initialize effective_solid based on real_solid and current_alpha. */
  m_effective_solid = m_real_solid;
  update_effective_solid ();

  reader.get("width", m_width);
  reader.get("height", m_height);
  if (m_width < 0 || m_height < 0) {
    //throw std::runtime_error("Invalid/No width/height specified in tilemap.");
    m_width = 0;
    m_height = 0;
    m_tiles.clear();
    resize(static_cast<int>(Sector::get().get_width() / 32.0f),
           static_cast<int>(Sector::get().get_height() / 32.0f));
    m_editor_active = false;
  } else {
    if (!reader.get("tiles", m_tiles))
      throw std::runtime_error("No tiles in tilemap.");

    if (int(m_tiles.size()) != m_width * m_height) {
      throw std::runtime_error("wrong number of tiles in tilemap.");
    }
  }

  bool empty = true;

  // make sure all tiles used on the tilemap are loaded and tilemap isn't empty
  for (const auto& tile : m_tiles) {
    if (tile != 0) {
      empty = false;
    }

    m_tileset->get(tile);
  }

  if (empty)
  {
    log_info << "Tilemap '" << get_name() << "', z-pos '" << m_z_pos << "' is empty." << std::endl;
  }
}

void
TileMap::finish_construction()
{
  if (get_path()) {
    Vector v = get_path()->get_base();
    set_offset(v);
  }

  m_add_path = get_walker() && get_path() && get_path()->is_valid();
}

TileMap::~TileMap()
{
}

void
TileMap::float_channel(float target, float &current, float remaining_time, float dt_sec)
{
  float amt = (target - current) / (remaining_time / dt_sec);
  if (amt > 0) current = std::min(current + amt, target);
  if (amt < 0) current = std::max(current + amt, target);
}

ObjectSettings
TileMap::get_settings()
{
  m_new_size_x = m_width;
  m_new_size_y = m_height;
  m_new_offset_x = 0;
  m_new_offset_y = 0;

  ObjectSettings result = GameObject::get_settings();

  result.add_bool(_("Solid"), &m_real_solid, "solid");
  result.add_int(_("Resize offset x"), &m_new_offset_x);
  result.add_int(_("Resize offset y"), &m_new_offset_y);

  result.add_int(_("Width"), &m_new_size_x);
  result.add_int(_("Height"), &m_new_size_y);

  result.add_float(_("Alpha"), &m_alpha, "alpha", 1.0f);
  result.add_float(_("Speed x"), &m_speed_x, "speed-x", 1.0f);
  result.add_float(_("Speed y"), &m_speed_y, "speed-y", 1.0f);
  result.add_color(_("Tint"), &m_tint, "tint", Color::WHITE);
  result.add_int(_("Z-pos"), &m_z_pos, "z-pos");
  result.add_enum(_("Draw target"), reinterpret_cast<int*>(&m_draw_target),
                  {_("Normal"), _("Lightmap")},
                  {"normal", "lightmap"},
                  static_cast<int>(DrawingTarget::COLORMAP),
                  "draw-target");

  result.add_path_ref(_("Path"), get_path_ref(), "path-ref");
  m_add_path = get_walker() && get_path() && get_path()->is_valid();
  result.add_bool(_("Following path"), &m_add_path);

  if (get_walker() && get_path() && get_path()->is_valid()) {
    m_running = get_walker()->is_running();
    result.add_walk_mode(_("Path Mode"), &get_path()->m_mode, {}, {});
    result.add_bool(_("Running"), &m_running, "running", false);
  }

  result.add_tiles(_("Tiles"), this, "tiles");

  result.reorder({"solid", "running", "speed-x", "speed-y", "tint", "draw-target", "alpha", "z-pos", "name", "path-ref", "width", "height", "tiles"});

  if (!m_editor_active) {
    result.add_remove();
  }

  return result;
}

void
TileMap::after_editor_set()
{
  if ((m_new_size_x != m_width || m_new_size_y != m_height ||
      m_new_offset_x || m_new_offset_y) &&
      m_new_size_x > 0 && m_new_size_y > 0) {
    resize(m_new_size_x, m_new_size_y, 0, m_new_offset_x, m_new_offset_y);
  }

  if (get_walker() && get_path() && get_path()->is_valid()) {
    if (!m_add_path) {
      get_path()->m_nodes.clear();
    }
  } else {
    if (m_add_path) {
      init_path_pos(m_offset, m_running);
    }
  }

  m_current_tint = m_tint;
  m_current_alpha = m_alpha;
}

void
TileMap::update(float dt_sec)
{
  // handle tilemap fading
  if (m_current_alpha != m_alpha) {
    m_remaining_fade_time = std::max(0.0f, m_remaining_fade_time - dt_sec);
    if (m_remaining_fade_time == 0.0f) {
      m_current_alpha = m_alpha;
    } else {
      float_channel(m_alpha, m_current_alpha, m_remaining_fade_time, dt_sec);
    }
    update_effective_solid ();
  }

  // handle tint fading
  if (m_current_tint.red != m_tint.red || m_current_tint.green != m_tint.green ||
      m_current_tint.blue != m_tint.blue || m_current_tint.alpha != m_tint.alpha) {

    m_remaining_tint_fade_time = std::max(0.0f, m_remaining_tint_fade_time - dt_sec);
    if (m_remaining_tint_fade_time == 0.0f) {
      m_current_tint = m_tint;
    } else {
      float_channel(m_tint.red  , m_current_tint.red  , m_remaining_tint_fade_time, dt_sec);
      float_channel(m_tint.green, m_current_tint.green, m_remaining_tint_fade_time, dt_sec);
      float_channel(m_tint.blue , m_current_tint.blue , m_remaining_tint_fade_time, dt_sec);
      float_channel(m_tint.alpha, m_current_tint.alpha, m_remaining_tint_fade_time, dt_sec);
    }
  }

  m_movement = Vector(0,0);
  // if we have a path to follow, follow it
  if (get_walker()) {
    get_walker()->update(dt_sec);
    Vector v = get_walker()->get_pos();
    if (get_path() && get_path()->is_valid()) {
      m_movement = v - get_offset();
      set_offset(v);
    } else {
      set_offset(Vector(0, 0));
    }
  }
}

void
TileMap::editor_update()
{
  if (get_walker()) {
    if (get_path() && get_path()->is_valid()) {
      m_movement = get_walker()->get_pos() - get_offset();
      set_offset(get_walker()->get_pos());
    } else {
      set_offset(Vector(0, 0));
    }
  }
}

void
TileMap::draw(DrawingContext& context)
{
  // skip draw if current opacity is 0.0
  if (m_current_alpha == 0.0f) return;

  context.push_transform();

  if (m_flip != NO_FLIP) context.set_flip(m_flip);

  if (m_editor_active) {
    if (m_current_alpha != 1.0f) {
      context.set_alpha(m_current_alpha);
    }
  } else {
    context.set_alpha(m_current_alpha/2);
  }

  // Force the translation to be an integer so that the tiles appear sharper.
  // For consistency (i.e., to avoid 1-pixel gaps), this needs to be done even
  // for solid tilemaps that are guaranteed to have speed 1.
  // FIXME Force integer translation for all graphics, not just tilemaps.
  float trans_x = roundf(context.get_translation().x);
  float trans_y = roundf(context.get_translation().y);
  bool normal_speed = m_editor_active && Editor::is_active();
  context.set_translation(Vector(std::truncf(trans_x * (normal_speed ? 1.0f : m_speed_x)),
                                 std::truncf(trans_y * (normal_speed ? 1.0f : m_speed_y))));

  Rectf draw_rect = context.get_cliprect();
  Rect t_draw_rect = get_tiles_overlapping(draw_rect);
  Vector start = get_tile_position(t_draw_rect.left, t_draw_rect.top);

  Vector pos;
  int tx, ty;

  std::unordered_map<SurfacePtr,
                     std::tuple<std::vector<Rectf>,
                                std::vector<Rectf>>> batches;

  for (pos.x = start.x, tx = t_draw_rect.left; tx < t_draw_rect.right; pos.x += 32, ++tx) {
    for (pos.y = start.y, ty = t_draw_rect.top; ty < t_draw_rect.bottom; pos.y += 32, ++ty) {
      int index = ty*m_width + tx;
      assert (index >= 0);
      assert (index < (m_width * m_height));

      if (m_tiles[index] == 0) continue;
      const Tile& tile = m_tileset->get(m_tiles[index]);

      if (g_debug.show_collision_rects) {
        tile.draw_debug(context.color(), pos, LAYER_FOREGROUND1);
      }

      const SurfacePtr& surface = Editor::current() ? tile.get_current_editor_surface() : tile.get_current_surface();
      if (surface) {
        std::get<0>(batches[surface]).emplace_back(surface->get_region());
        std::get<1>(batches[surface]).emplace_back(pos,
                                                   Sizef(static_cast<float>(surface->get_width()),
                                                         static_cast<float>(surface->get_height())));
      }
    }
  }

  Canvas& canvas = context.get_canvas(m_draw_target);

  for (auto& it : batches)
  {
    const SurfacePtr& surface = it.first;
    if (surface) {
      canvas.draw_surface_batch(surface,
                                std::move(std::get<0>(it.second)),
                                std::move(std::get<1>(it.second)),
                                m_current_tint, m_z_pos);
    }
  }

  context.pop_transform();
}

void
TileMap::goto_node(int node_no)
{
  if (!get_walker()) return;
  get_walker()->goto_node(node_no);
}

void
TileMap::start_moving()
{
  if (!get_walker()) return;
  get_walker()->start_moving();
}

void
TileMap::stop_moving()
{
  if (!get_walker()) return;
  get_walker()->stop_moving();
}

void
TileMap::set(int newwidth, int newheight, const std::vector<unsigned int>&newt,
             int new_z_pos, bool newsolid)
{
  if (int(newt.size()) != newwidth * newheight)
    throw std::runtime_error("Wrong tilecount count.");

  m_width  = newwidth;
  m_height = newheight;

  m_tiles.resize(newt.size());
  m_tiles = newt;

  if (new_z_pos > (LAYER_GUI - 100))
    m_z_pos = LAYER_GUI - 100;
  else
    m_z_pos  = new_z_pos;
  m_real_solid  = newsolid;
  update_effective_solid ();

  // make sure all tiles are loaded
  for (const auto& tile : m_tiles)
    m_tileset->get(tile);
}

void
TileMap::resize(int new_width, int new_height, int fill_id,
                int xoffset, int yoffset)
{
  if (new_width < m_width) {
    // remap tiles for new width
    for (int y = 0; y < m_height && y < new_height; ++y) {
      for (int x = 0; x < new_width; ++x) {
        m_tiles[y * new_width + x] = m_tiles[y * m_width + x];
      }
    }
  }

  m_tiles.resize(new_width * new_height, fill_id);

  if (new_width > m_width) {
    // remap tiles
    for (int y = std::min(m_height, new_height)-1; y >= 0; --y) {
      for (int x = new_width-1; x >= 0; --x) {
        if (x >= m_width) {
          m_tiles[y * new_width + x] = fill_id;
          continue;
        }

        m_tiles[y * new_width + x] = m_tiles[y * m_width + x];
      }
    }
  }

  m_height = new_height;
  m_width = new_width;

  //Apply offset
  if (xoffset || yoffset) {
    for (int y = 0; y < m_height; y++) {
      int Y = (yoffset < 0) ? y : (m_height - y - 1);
      for (int x = 0; x < m_width; x++) {
        int X = (xoffset < 0) ? x : (m_width - x - 1);
        if (Y - yoffset < 0 || Y - yoffset >= m_height ||
            X - xoffset < 0 || X - xoffset >= m_width) {
          m_tiles[Y * new_width + X] = fill_id;
        } else {
          m_tiles[Y * new_width + X] = m_tiles[(Y - yoffset) * m_width + X - xoffset];
        }
      }
    }
  }
}

void TileMap::resize(const Size& newsize, const Size& resize_offset) {
  resize(newsize.width, newsize.height, 0, resize_offset.width, resize_offset.height);
}

Rect
TileMap::get_tiles_overlapping(const Rectf &rect) const
{
  Rectf rect2 = rect;
  rect2.move(-m_offset);

  int t_left   = std::max(0     , int(floorf(rect2.get_left  () / 32)));
  int t_right  = std::min(m_width , int(ceilf (rect2.get_right () / 32)));
  int t_top    = std::max(0     , int(floorf(rect2.get_top   () / 32)));
  int t_bottom = std::min(m_height, int(ceilf (rect2.get_bottom() / 32)));
  return Rect(t_left, t_top, t_right, t_bottom);
}

void
TileMap::set_solid(bool solid)
{
  m_real_solid = solid;
  update_effective_solid ();
}

uint32_t
TileMap::get_tile_id(int x, int y) const
{
  if (x < 0 || x >= m_width || y < 0 || y >= m_height) {
    //log_warning << "tile outside tilemap requested" << std::endl;
    return 0;
  }

  return m_tiles[y*m_width + x];
}

const Tile&
TileMap::get_tile(int x, int y) const
{
  uint32_t id = get_tile_id(x, y);
  return m_tileset->get(id);
}

uint32_t
TileMap::get_tile_id_at(const Vector& pos) const
{
  Vector xy = (pos - m_offset) / 32;
  return get_tile_id(int(xy.x), int(xy.y));
}

const Tile&
TileMap::get_tile_at(const Vector& pos) const
{
  uint32_t id = get_tile_id_at(pos);
  return m_tileset->get(id);
}

void
TileMap::change(int x, int y, uint32_t newtile)
{
  assert(x >= 0 && x < m_width && y >= 0 && y < m_height);
  m_tiles[y*m_width + x] = newtile;
}

void
TileMap::change_at(const Vector& pos, uint32_t newtile)
{
  Vector xy = (pos - m_offset) / 32;
  change(int(xy.x), int(xy.y), newtile);
}

void
TileMap::change_all(uint32_t oldtile, uint32_t newtile)
{
  for (int x = 0; x < get_width(); x++) {
    for (int y = 0; y < get_height(); y++) {
      if (get_tile_id(x,y) != oldtile)
        continue;

      change(x,y,newtile);
    }
  }
}

void
TileMap::fade(float alpha_, float seconds)
{
  m_alpha = alpha_;
  m_remaining_fade_time = seconds;
}

void
TileMap::tint_fade(const Color& new_tint, float seconds)
{
  m_tint = new_tint;
  m_remaining_tint_fade_time = seconds;
}

void
TileMap::set_alpha(float alpha_)
{
  m_alpha = alpha_;
  m_current_alpha = m_alpha;
  m_remaining_fade_time = 0;
  update_effective_solid ();
}

float
TileMap::get_alpha() const
{
  return m_current_alpha;
}

void
TileMap::move_by(const Vector& shift)
{
  if (!get_path()) {
    init_path_pos(m_offset);
    m_add_path = true;
  }
  get_path()->move_by(shift);
  m_offset += shift;
}

void
TileMap::update_effective_solid()
{
  if (!m_real_solid)
    m_effective_solid = false;
  else if (m_effective_solid && (m_current_alpha < 0.25f))
    m_effective_solid = false;
  else if (!m_effective_solid && (m_current_alpha >= 0.75f))
    m_effective_solid = true;
}

void
TileMap::set_tileset(const TileSet* new_tileset)
{
  m_tileset = new_tileset;
}

/* EOF */
