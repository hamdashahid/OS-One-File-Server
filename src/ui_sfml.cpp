#include <SFML/Graphics.hpp>
#include <map>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "ui_shared.h"
#include "parking.h"

using std::deque;
using std::map;
using std::string;
using std::vector;

// Vehicle states
enum class VState
{
    Approaching,
    Waiting,
    Crossing,
    Parked,
    Leaving,
    Inactive
};

struct VisualVehicle
{
    int id;
    VehicleType type;
    IntersectionId from, to;
    Direction dir;
    sf::Vector2f startPos, stopLinePos, crossEndPos;
    bool useBezier;
    sf::Vector2f p0, p1, p2, p3;
    float t;
    sf::Color color;
    VState state;
    string stateName;
    float pulseTime; // For emergency vehicle animation
};

struct EventLog
{
    string message;
    float lifetime;
};

struct Stats
{
    int totalVehicles = 0;
    int completed = 0;
    int emergencyCount = 0;
    int parkedCount = 0;
};

static std::thread g_ui_thread;
static std::mutex g_mutex;
static std::atomic<bool> g_running(false);
static vector<VisualVehicle> g_cars;
static map<IntersectionId, LightColor> g_lights;
static map<IntersectionId, bool> g_preempts;
static deque<EventLog> g_events;
static Stats g_stats;
static float g_timeElapsed = 0.f;

// Adjusted window dimensions
static const unsigned WINDOW_W = 1220;
static const unsigned WINDOW_H = 600;
static const sf::Vector2f F10_POS = {280.f, 300.f};
static const sf::Vector2f F11_POS = {720.f, 300.f};
static const float ROAD_WIDTH = 100.f;
static const float INTERSIZE = 120.f;

static sf::Font *g_font = nullptr;
static bool g_font_loaded = false;

static void tryLoadFont()
{
    if (g_font_loaded)
        return;
    if (!g_font)
        g_font = new sf::Font();
    vector<string> paths = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc"};
    for (auto &p : paths)
    {
        if (g_font->loadFromFile(p))
        {
            g_font_loaded = true;
            break;
        }
    }
}

static sf::Color vehicleColor(VehicleType t)
{
    switch (t)
    {
    case VehicleType::Ambulance:
        return sf::Color(255, 40, 40);
    case VehicleType::FireTruck:
        return sf::Color(255, 90, 20);
    case VehicleType::Bus:
        return sf::Color(255, 200, 0);
    case VehicleType::Car:
        return sf::Color(60, 120, 255);
    case VehicleType::Bike:
        return sf::Color(80, 230, 80);
    case VehicleType::Tractor:
        return sf::Color(110, 180, 70);
    }
    return sf::Color::White;
}

static string typeToString(VehicleType t)
{
    switch (t)
    {
    case VehicleType::Ambulance:
        return "Ambulance";
    case VehicleType::FireTruck:
        return "FireTruck";
    case VehicleType::Bus:
        return "Bus";
    case VehicleType::Car:
        return "Car";
    case VehicleType::Bike:
        return "Bike";
    case VehicleType::Tractor:
        return "Tractor";
    }
    return "Unknown";
}

static string stateToString(VState s)
{
    switch (s)
    {
    case VState::Approaching:
        return "Approaching";
    case VState::Waiting:
        return "Waiting";
    case VState::Crossing:
        return "Crossing";
    case VState::Parked:
        return "Parked";
    case VState::Leaving:
        return "Leaving";
    default:
        return "Inactive";
    }
}

static sf::Vector2f bezierPoint(const sf::Vector2f &p0, const sf::Vector2f &p1,
                                const sf::Vector2f &p2, const sf::Vector2f &p3, float t)
{
    float u = 1.f - t;
    float tt = t * t, uu = u * u;
    float uuu = uu * u, ttt = tt * t;
    return uuu * p0 + 3.f * uu * t * p1 + 3.f * u * tt * p2 + ttt * p3;
}

static void drawText(sf::RenderWindow &win, const string &txt, const sf::Vector2f &pos,
                     int size, const sf::Color &col, bool bold = false)
{
    if (!g_font_loaded || !g_font)
        return;
    sf::Text t;
    t.setFont(*g_font);
    t.setCharacterSize(size);
    t.setString(txt);
    t.setFillColor(col);
    if (bold)
        t.setStyle(sf::Text::Bold);
    t.setPosition(pos);
    win.draw(t);
}

static void drawGradientRect(sf::RenderWindow &win, const sf::Vector2f &pos, const sf::Vector2f &size,
                             const sf::Color &top, const sf::Color &bottom)
{
    sf::VertexArray quad(sf::Quads, 4);
    quad[0].position = pos;
    quad[1].position = sf::Vector2f(pos.x + size.x, pos.y);
    quad[2].position = pos + size;
    quad[3].position = sf::Vector2f(pos.x, pos.y + size.y);
    quad[0].color = quad[1].color = top;
    quad[2].color = quad[3].color = bottom;
    win.draw(quad);
}

// Draw header bar
static void drawHeader(sf::RenderWindow &win)
{
    // Gradient background
    drawGradientRect(win, sf::Vector2f(0, 0), sf::Vector2f(WINDOW_W, 50),
                     sf::Color(25, 35, 50), sf::Color(15, 20, 30));

    // Border
    sf::RectangleShape border(sf::Vector2f(WINDOW_W, 2));
    border.setPosition(0, 58);
    border.setFillColor(sf::Color(100, 150, 200));
    win.draw(border);

    // Title with glow effect
    drawText(win, "TRAFFIC SIMULATION SYSTEM", sf::Vector2f(20, 8), 22, sf::Color(100, 200, 255), true);
    drawText(win, "F10 & F11 Intersections", sf::Vector2f(22, 32), 11, sf::Color(180, 180, 200));

    // Stats in header
    std::ostringstream oss;
    oss << "Time: " << std::fixed << std::setprecision(1) << g_timeElapsed << "s";
    drawText(win, oss.str(), sf::Vector2f(WINDOW_W - 250, 15), 12, sf::Color(200, 200, 200));

    oss.str("");
    oss << "Vehicles: " << g_stats.completed << "/" << g_stats.totalVehicles;
    drawText(win, oss.str(), sf::Vector2f(WINDOW_W - 250, 33), 12, sf::Color(200, 200, 200));
}

// Draw modern road network
static void drawRoads(sf::RenderWindow &win)
{
    // Main highway with gradient
    sf::RectangleShape road(sf::Vector2f((F11_POS.x - F10_POS.x) + 300.f, ROAD_WIDTH));
    road.setOrigin(150.f, ROAD_WIDTH / 2.f);
    road.setPosition(F10_POS);
    road.setFillColor(sf::Color(40, 40, 45));
    road.setOutlineThickness(3.f);
    road.setOutlineColor(sf::Color(80, 80, 90));
    win.draw(road);

    // Lane markings (dashed)
    for (float x = F10_POS.x - 150.f; x < F11_POS.x + 150.f; x += 40.f)
    {
        sf::RectangleShape dash(sf::Vector2f(20.f, 4.f));
        dash.setOrigin(10.f, 2.f);
        dash.setPosition(x, F10_POS.y);
        dash.setFillColor(sf::Color(220, 220, 100, 200));
        win.draw(dash);
    }

    // Stop lines at intersections
    sf::RectangleShape stop(sf::Vector2f(8.f, ROAD_WIDTH * 0.8f));
    stop.setOrigin(4.f, ROAD_WIDTH * 0.4f);
    stop.setFillColor(sf::Color(255, 255, 255));

    // F10 stop lines
    stop.setPosition(F10_POS + sf::Vector2f(-100.f, 0.f));
    win.draw(stop);
    stop.setPosition(F10_POS + sf::Vector2f(100.f, 0.f));
    win.draw(stop);

    // F11 stop lines
    stop.setPosition(F11_POS + sf::Vector2f(-100.f, 0.f));
    win.draw(stop);
    stop.setPosition(F11_POS + sf::Vector2f(100.f, 0.f));
    win.draw(stop);

    // Sidewalks
    sf::RectangleShape sidewalk(sf::Vector2f((F11_POS.x - F10_POS.x) + 300.f, 20.f));
    sidewalk.setOrigin(150.f, 10.f);
    sidewalk.setPosition(F10_POS + sf::Vector2f(0, -ROAD_WIDTH / 2 - 15));
    sidewalk.setFillColor(sf::Color(60, 60, 60));
    win.draw(sidewalk);
    sidewalk.setPosition(F10_POS + sf::Vector2f(0, ROAD_WIDTH / 2 + 15));
    win.draw(sidewalk);
}

// Draw animated intersection
static void drawIntersection(sf::RenderWindow &win, IntersectionId id, const sf::Vector2f &pos, float time)
{
    LightColor light = g_lights[id];
    bool preempt = g_preempts[id];

    // Intersection platform with shadow
    sf::CircleShape shadow(INTERSIZE * 0.6f);
    shadow.setOrigin(INTERSIZE * 0.6f, INTERSIZE * 0.6f);
    shadow.setPosition(pos + sf::Vector2f(5, 5));
    shadow.setFillColor(sf::Color(0, 0, 0, 60));
    win.draw(shadow);

    // Main intersection circle
    sf::CircleShape inter(INTERSIZE * 0.55f);
    inter.setOrigin(INTERSIZE * 0.55f, INTERSIZE * 0.55f);
    inter.setPosition(pos);
    inter.setFillColor(sf::Color(55, 55, 65));
    inter.setOutlineThickness(4.f);

    if (preempt)
    {
        // Pulsing orange for emergency
        float pulse = 0.5f + 0.5f * std::sin(time * 5.f);
        inter.setOutlineColor(sf::Color(255, 100 + pulse * 50, 0));
    }
    else
    {
        inter.setOutlineColor(sf::Color(120, 120, 130));
    }
    win.draw(inter);

    // Traffic signal pole
    sf::RectangleShape pole(sf::Vector2f(8, 80));
    pole.setOrigin(4, 80);
    pole.setPosition(pos + sf::Vector2f(-95.f, -20.f));
    pole.setFillColor(sf::Color(80, 80, 80));
    win.draw(pole);

    // Traffic light housing
    sf::RectangleShape housing(sf::Vector2f(35, 90));
    housing.setOrigin(17.5f, 45);
    housing.setPosition(pos + sf::Vector2f(-95.f, -80.f));
    housing.setFillColor(sf::Color(40, 40, 40));
    housing.setOutlineThickness(2);
    if (preempt)
    {
        // Flashing red border for emergency preemption
        float flash = (std::sin(time * 6.f) > 0.f) ? 1.f : 0.3f;
        housing.setOutlineColor(sf::Color(255 * flash, 50, 50));
    }
    else
    {
        housing.setOutlineColor(sf::Color(20, 20, 20));
    }
    win.draw(housing);

    // Traffic lights
    sf::CircleShape redLight(12.f);
    redLight.setOrigin(12.f, 12.f);
    redLight.setPosition(pos + sf::Vector2f(-95.f, -105.f));
    redLight.setFillColor(light == LightColor::RED ? sf::Color(255, 50, 50) : sf::Color(80, 20, 20));
    if (light == LightColor::RED)
    {
        redLight.setOutlineThickness(3);
        redLight.setOutlineColor(sf::Color(255, 150, 150, 150));
    }
    win.draw(redLight);

    sf::CircleShape greenLight(12.f);
    greenLight.setOrigin(12.f, 12.f);
    greenLight.setPosition(pos + sf::Vector2f(-95.f, -55.f));
    greenLight.setFillColor(light == LightColor::GREEN ? sf::Color(50, 255, 50) : sf::Color(20, 80, 20));
    if (light == LightColor::GREEN)
    {
        greenLight.setOutlineThickness(3);
        greenLight.setOutlineColor(sf::Color(150, 255, 150, 150));
    }
    win.draw(greenLight);

    // Intersection label with background
    sf::RectangleShape labelBg(sf::Vector2f(80, 35));
    labelBg.setOrigin(40, 17.5);
    labelBg.setPosition(pos);
    labelBg.setFillColor(sf::Color(30, 40, 50, 230));
    labelBg.setOutlineThickness(2);
    labelBg.setOutlineColor(sf::Color(100, 150, 200));
    win.draw(labelBg);

    string name = (id == IntersectionId::F10) ? "F10" : "F11";
    drawText(win, name, pos + sf::Vector2f(-20.f, -10.f), 22, sf::Color::White, true);

    // Emergency preempt indicator
    if (preempt)
    {
        sf::RectangleShape alertBg(sf::Vector2f(120, 25));
        alertBg.setOrigin(60, 12.5);
        alertBg.setPosition(pos + sf::Vector2f(0, 50));
        alertBg.setFillColor(sf::Color(255, 100, 0, 200));
        win.draw(alertBg);
        drawText(win, "EMERGENCY", pos + sf::Vector2f(-45.f, 42.f), 14, sf::Color::White, true);
    }
}

// Draw parking lot with modern design - MORE COMPACT
static void drawParking(sf::RenderWindow &win, const ParkingLot &lot, const sf::Vector2f &base)
{
    const float slotW = 30.f, slotH = 20.f, gap = 5.f; // Reduced sizes
    const float totalW = 5 * slotW + 6 * gap;
    const float totalH = 2 * slotH + 3 * gap + 38.f; // Reduced header

    // Shadow
    sf::RectangleShape shadow(sf::Vector2f(totalW, totalH));
    shadow.setPosition(base + sf::Vector2f(3, 3));
    shadow.setFillColor(sf::Color(0, 0, 0, 40));
    win.draw(shadow);

    // Background card
    sf::RectangleShape card(sf::Vector2f(totalW, totalH));
    card.setPosition(base);
    card.setFillColor(sf::Color(30, 35, 45));
    card.setOutlineThickness(2.f); // Reduced thickness
    card.setOutlineColor(sf::Color(80, 90, 110));
    win.draw(card);

    // Header bar - REDUCED
    sf::RectangleShape header(sf::Vector2f(totalW, 28)); // Reduced from 35
    header.setPosition(base);
    header.setFillColor(sf::Color(45, 55, 70));
    win.draw(header);

    // Parking icon
    drawText(win, "P", base + sf::Vector2f(6.f, 3.f), 18, sf::Color(100, 200, 255), true); // Reduced

    // Title - determine from name
    string shortName = lot.name;
    if (shortName.find("F10") != string::npos)
        shortName = "F10 Parking";
    else if (shortName.find("F11") != string::npos)
        shortName = "F11 Parking";
    drawText(win, shortName, base + sf::Vector2f(28.f, 6.f), 12, sf::Color::White, true); // Reduced

    int occupied = 0;
    {
        auto *pl = const_cast<ParkingLot *>(&lot);
        int lock_result = pthread_mutex_trylock(&pl->state_lock);
        if (lock_result == 0)
        {
            occupied = pl->current_spots;
            pthread_mutex_unlock(&pl->state_lock);
        }
    }

    // Parking spots
    int idx = 0;
    for (int r = 0; r < 2; ++r)
    {
        for (int c = 0; c < 5; ++c)
        {
            sf::RectangleShape slot(sf::Vector2f(slotW, slotH));
            slot.setPosition(base + sf::Vector2f(gap + c * (slotW + gap), 33.f + gap + r * (slotH + gap))); // Adjusted

            if (idx < occupied)
            {
                slot.setFillColor(sf::Color(220, 60, 60));
                // Draw car icon
                sf::RectangleShape car(sf::Vector2f(slotW * 0.7f, slotH * 0.6f));
                car.setPosition(slot.getPosition() + sf::Vector2f(slotW * 0.15f, slotH * 0.2f));
                car.setFillColor(sf::Color(180, 40, 40));
                win.draw(car);
            }
            else
            {
                slot.setFillColor(sf::Color(60, 180, 60));
            }

            slot.setOutlineThickness(1.5f); // Reduced
            slot.setOutlineColor(sf::Color(40, 40, 45));
            win.draw(slot);
            ++idx;
        }
    }

    // Status indicator
    std::ostringstream oss;
    oss << occupied << "/" << lot.max_spots;
    sf::Color statusColor = occupied >= 8 ? sf::Color(255, 100, 100) : occupied >= 5 ? sf::Color(255, 200, 100)
                                                                                     : sf::Color(100, 255, 100);
    drawText(win, oss.str(), base + sf::Vector2f(totalW - 45.f, 6.f), 12, statusColor, true); // Reduced
}

// Draw vehicle with enhanced graphics
static void drawVehicles(sf::RenderWindow &win, float dt)
{
    std::lock_guard<std::mutex> lk(g_mutex);

    for (auto &c : g_cars)
    {
        if (c.state == VState::Inactive)
            continue;

        c.pulseTime += dt * 4.f;

        sf::Vector2f pos;
        switch (c.state)
        {
        case VState::Approaching:
        {
            c.t += dt * 0.25f; // Slower approach
            if (c.t > 1.f)
            {
                c.t = 1.f;
                c.state = VState::Waiting;
                c.stateName = stateToString(c.state);
            }
            pos = c.startPos + (c.stopLinePos - c.startPos) * c.t;
            break;
        }
        case VState::Waiting:
        {
            pos = c.stopLinePos;
            break;
        }
        case VState::Crossing:
        {
            c.t += dt * 0.35f; // Slower crossing
            if (c.t > 1.f)
            {
                c.t = 1.f;
            }
            if (c.useBezier)
            {
                pos = bezierPoint(c.p0, c.p1, c.p2, c.p3, c.t);
            }
            else
            {
                pos = c.stopLinePos + (c.crossEndPos - c.stopLinePos) * c.t;
            }
            break;
        }
        case VState::Parked:
        {
            // Position vehicle at the parking lot
            sf::Vector2f parkBase = (c.from == IntersectionId::F10) ? (F10_POS + sf::Vector2f(-150.f, 150.f)) : (F11_POS + sf::Vector2f(15.f, 150.f));
            // Center in parking area
            pos = parkBase + sf::Vector2f(100.f, 45.f);
            break;
        }
        case VState::Leaving:
        {
            c.t += dt * 0.4f; // Slower departure
            if (c.t > 1.f)
            {
                c.state = VState::Inactive;
            }
            float dir = (c.to == IntersectionId::F10) ? -1.f : 1.f;
            pos = c.crossEndPos + sf::Vector2f(dir * c.t * 120.f, 0.f);
            break;
        }
        case VState::Inactive:
        default:
            continue;
        }

        // Emergency vehicle glow/pulse
        if (c.type == VehicleType::Ambulance || c.type == VehicleType::FireTruck)
        {
            float pulse = 0.5f + 0.5f * std::sin(c.pulseTime);
            sf::CircleShape glow(22.f);
            glow.setOrigin(22.f, 22.f);
            glow.setPosition(pos);
            glow.setFillColor(sf::Color(255, 255, 255, 50 + pulse * 80));
            win.draw(glow);
        }

        // Vehicle shadow
        sf::CircleShape shadow(14.f);
        shadow.setOrigin(14.f, 14.f);
        shadow.setPosition(pos + sf::Vector2f(2, 2));
        shadow.setFillColor(sf::Color(0, 0, 0, 80));
        win.draw(shadow);

        // Vehicle body with better shading
        sf::CircleShape body(14.f);
        body.setOrigin(14.f, 14.f);
        body.setPosition(pos);
        body.setFillColor(c.color);
        body.setOutlineThickness(2.f);
        body.setOutlineColor(sf::Color(255, 255, 255, 180));
        win.draw(body);

        // Vehicle inner highlight
        sf::CircleShape highlight(6.f);
        highlight.setOrigin(6.f, 6.f);
        highlight.setPosition(pos + sf::Vector2f(-3, -3));
        highlight.setFillColor(sf::Color(255, 255, 255, 100));
        win.draw(highlight);

        // ID label with background
        std::ostringstream oss;
        oss << "#" << c.id;
        sf::RectangleShape idBg(sf::Vector2f(28, 18));
        idBg.setOrigin(14, 9);
        idBg.setPosition(pos + sf::Vector2f(0, -28));
        idBg.setFillColor(sf::Color(0, 0, 0, 180));
        idBg.setOutlineThickness(1);
        idBg.setOutlineColor(c.color);
        win.draw(idBg);
        drawText(win, oss.str(), pos + sf::Vector2f(-10.f, -34.f), 11, sf::Color::White, true);
    }

    g_cars.erase(std::remove_if(g_cars.begin(), g_cars.end(),
                                [](const VisualVehicle &v)
                                { return v.state == VState::Inactive; }),
                 g_cars.end());
}

// Draw comprehensive info panel - MORE COMPACT
static void drawInfoPanel(sf::RenderWindow &win)
{
    const float panelX = WINDOW_W - 220.f; // Slightly narrower
    const float panelY = 60.f;             // Reduced from 80
    const float panelW = 200.f;            // Reduced from 210
    const float panelH = WINDOW_H - 150.f; // More space at bottom

    // Shadow
    sf::RectangleShape shadow(sf::Vector2f(panelW, panelH));
    shadow.setPosition(panelX + 4, panelY + 4);
    shadow.setFillColor(sf::Color(0, 0, 0, 60));
    win.draw(shadow);

    // Panel background
    sf::RectangleShape panel(sf::Vector2f(panelW, panelH));
    panel.setPosition(panelX, panelY);
    panel.setFillColor(sf::Color(25, 30, 40, 245));
    panel.setOutlineThickness(2.5f); // Reduced
    panel.setOutlineColor(sf::Color(100, 120, 150));
    win.draw(panel);

    float yOffset = panelY + 10.f; // Reduced spacing

    // Panel title
    drawText(win, "SYSTEM STATUS", sf::Vector2f(panelX + 12.f, yOffset), 16, sf::Color(100, 200, 255), true); // Reduced
    yOffset += 25.f;                                                                                          // Reduced

    // Divider
    sf::RectangleShape divider(sf::Vector2f(panelW - 24, 2)); // Adjusted
    divider.setPosition(panelX + 12, yOffset);
    divider.setFillColor(sf::Color(100, 120, 150));
    win.draw(divider);
    yOffset += 10.f; // Reduced

    // Statistics section
    drawText(win, "Statistics", sf::Vector2f(panelX + 12.f, yOffset), 13, sf::Color(150, 200, 255), true); // Reduced
    yOffset += 18.f;                                                                                       // Reduced

    std::ostringstream oss;
    oss << "Total Vehicles: " << g_stats.totalVehicles;
    drawText(win, oss.str(), sf::Vector2f(panelX + 15.f, yOffset), 10, sf::Color(200, 200, 200)); // Reduced
    yOffset += 14.f;                                                                              // Reduced

    oss.str("");
    oss << "Completed: " << g_stats.completed;
    drawText(win, oss.str(), sf::Vector2f(panelX + 15.f, yOffset), 10, sf::Color(100, 255, 100));
    yOffset += 14.f;

    oss.str("");
    oss << "Active: " << (g_stats.totalVehicles - g_stats.completed);
    drawText(win, oss.str(), sf::Vector2f(panelX + 15.f, yOffset), 10, sf::Color(255, 200, 100));
    yOffset += 14.f;

    oss.str("");
    oss << "Emergency Count: " << g_stats.emergencyCount;
    drawText(win, oss.str(), sf::Vector2f(panelX + 15.f, yOffset), 10, sf::Color(255, 100, 100));
    yOffset += 18.f; // Reduced

    // Intersection status
    divider.setPosition(panelX + 12, yOffset);
    win.draw(divider);
    yOffset += 10.f;

    drawText(win, "Intersections", sf::Vector2f(panelX + 12.f, yOffset), 13, sf::Color(150, 200, 255), true);
    yOffset += 18.f;

    // F10 status - COMPACT
    drawText(win, "F10 Intersection", sf::Vector2f(panelX + 15.f, yOffset), 11, sf::Color::Cyan, true); // Reduced
    yOffset += 16.f;                                                                                    // Reduced

    string f10Light = (g_lights[IntersectionId::F10] == LightColor::GREEN) ? "GREEN" : "RED";
    sf::Color f10Col = (g_lights[IntersectionId::F10] == LightColor::GREEN) ? sf::Color(80, 255, 80) : sf::Color(255, 80, 80);

    sf::CircleShape statusDot(4); // Reduced size
    statusDot.setPosition(panelX + 20, yOffset + 2);
    statusDot.setFillColor(f10Col);
    win.draw(statusDot);

    drawText(win, "Signal: " + f10Light, sf::Vector2f(panelX + 30.f, yOffset), 9, f10Col); // Reduced
    yOffset += 14.f;                                                                       // Reduced

    if (g_preempts[IntersectionId::F10])
    {
        drawText(win, "Status: PREEMPTED", sf::Vector2f(panelX + 30.f, yOffset), 9, sf::Color(255, 150, 0), true);
        yOffset += 14.f;
    }
    yOffset += 6.f;

    // F11 status - COMPACT
    drawText(win, "F11 Intersection", sf::Vector2f(panelX + 15.f, yOffset), 11, sf::Color::Cyan, true);
    yOffset += 16.f;

    string f11Light = (g_lights[IntersectionId::F11] == LightColor::GREEN) ? "GREEN" : "RED";
    sf::Color f11Col = (g_lights[IntersectionId::F11] == LightColor::GREEN) ? sf::Color(80, 255, 80) : sf::Color(255, 80, 80);

    statusDot.setPosition(panelX + 20, yOffset + 2);
    statusDot.setFillColor(f11Col);
    win.draw(statusDot);

    drawText(win, "Signal: " + f11Light, sf::Vector2f(panelX + 30.f, yOffset), 9, f11Col);
    yOffset += 14.f;

    if (g_preempts[IntersectionId::F11])
    {
        drawText(win, "Status: PREEMPTED", sf::Vector2f(panelX + 30.f, yOffset), 9, sf::Color(255, 150, 0), true);
        yOffset += 14.f;
    }
    yOffset += 10.f;

    // Active vehicles - COMPACT
    divider.setPosition(panelX + 12, yOffset);
    win.draw(divider);
    yOffset += 10.f;

    drawText(win, "Active Vehicles", sf::Vector2f(panelX + 12.f, yOffset), 13, sf::Color(150, 200, 255), true);
    yOffset += 18.f;

    std::lock_guard<std::mutex> lk(g_mutex);
    int count = 0;
    for (auto &c : g_cars)
    {
        if (c.state == VState::Inactive)
            continue;
        if (count >= 6)
        { // Reduced from 8 to 6
            drawText(win, "... and more", sf::Vector2f(panelX + 15.f, yOffset), 9, sf::Color(150, 150, 150));
            break;
        }

        // Vehicle color indicator - SMALLER
        sf::RectangleShape colorBox(sf::Vector2f(8, 8)); // Reduced
        colorBox.setPosition(panelX + 15, yOffset + 1);
        colorBox.setFillColor(c.color);
        colorBox.setOutlineThickness(1);
        colorBox.setOutlineColor(sf::Color::White);
        win.draw(colorBox);

        std::ostringstream voss;
        voss << "#" << c.id << " " << typeToString(c.type).substr(0, 6);                      // Shorter
        drawText(win, voss.str(), sf::Vector2f(panelX + 28.f, yOffset), 9, sf::Color::White); // Reduced

        drawText(win, c.stateName, sf::Vector2f(panelX + 145.f, yOffset), 8, sf::Color(180, 180, 180)); // Reduced
        yOffset += 13.f;                                                                                // Reduced spacing
        count++;
    }

    yOffset += 6.f;

    // Event log - COMPACT
    divider.setPosition(panelX + 12, yOffset);
    win.draw(divider);
    yOffset += 10.f;

    drawText(win, "Recent Events", sf::Vector2f(panelX + 12.f, yOffset), 13, sf::Color(150, 200, 255), true);
    yOffset += 18.f;

    int evCount = 0;
    for (auto &ev : g_events)
    {
        if (evCount >= 8 || yOffset > panelY + panelH - 15)
            break; // Reduced from 10 to 8
        float alpha = std::min(255.f, ev.lifetime * 50.f);
        drawText(win, ev.message, sf::Vector2f(panelX + 15.f, yOffset), 8, // Reduced font
                 sf::Color(200, 200, 200, static_cast<sf::Uint8>(alpha)));
        yOffset += 12.f; // Reduced spacing
        evCount++;
    }
}

// Draw legend - MORE COMPACT
static void drawLegend(sf::RenderWindow &win)
{
    const float legX = 20.f;
    const float legY = WINDOW_H - 95.f; // Reduced from 115

    sf::RectangleShape bg(sf::Vector2f(280, 75)); // Reduced size
    bg.setPosition(legX, legY);
    bg.setFillColor(sf::Color(20, 25, 35, 230));
    bg.setOutlineThickness(2);
    bg.setOutlineColor(sf::Color(80, 90, 110));
    win.draw(bg);

    drawText(win, "LEGEND", sf::Vector2f(legX + 8, legY + 4), 12, sf::Color(100, 200, 255), true); // Reduced

    float y = legY + 22; // Reduced

    // Emergency
    sf::CircleShape dot(4); // Reduced size
    dot.setPosition(legX + 12, y);
    dot.setFillColor(sf::Color(255, 50, 50));
    win.draw(dot);
    drawText(win, "Emergency (Ambulance/FireTruck)", sf::Vector2f(legX + 22, y - 3), 8, sf::Color::White); // Reduced
    y += 14;                                                                                               // Reduced spacing

    // Bus
    dot.setPosition(legX + 12, y);
    dot.setFillColor(sf::Color(255, 200, 0));
    win.draw(dot);
    drawText(win, "Medium Priority (Bus)", sf::Vector2f(legX + 22, y - 3), 8, sf::Color::White);
    y += 14;

    // Normal
    dot.setPosition(legX + 12, y);
    dot.setFillColor(sf::Color(70, 130, 255));
    win.draw(dot);
    drawText(win, "Normal (Car/Bike/Tractor)", sf::Vector2f(legX + 22, y - 3), 8, sf::Color::White);
}

static void updateEvents(float dt)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto &ev : g_events)
    {
        ev.lifetime -= dt;
    }
    g_events.erase(std::remove_if(g_events.begin(), g_events.end(),
                                  [](const EventLog &e)
                                  { return e.lifetime <= 0.f; }),
                   g_events.end());
}

static void computePath(VisualVehicle &c)
{
    // Same bezier path computation as before
    c.useBezier = (c.dir == Direction::Left || c.dir == Direction::Right);
    if (c.useBezier)
    {
        sf::Vector2f mid = (c.from == IntersectionId::F10) ? F10_POS : F11_POS;
        c.p0 = c.stopLinePos;
        if (c.dir == Direction::Left)
        {
            c.p1 = c.p0 + sf::Vector2f(30, 0);
            c.p2 = mid + sf::Vector2f(0, -50);
            c.p3 = mid + sf::Vector2f(0, -100);
        }
        else
        {
            c.p1 = c.p0 + sf::Vector2f(30, 0);
            c.p2 = mid + sf::Vector2f(0, 50);
            c.p3 = mid + sf::Vector2f(0, 100);
        }
        c.crossEndPos = c.p3;
    }
}

static void addApproachVehicle(IntersectionId id, Vehicle *v)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    VisualVehicle vc;
    vc.id = v->id;
    vc.type = v->type;
    vc.from = v->originIntersection;
    vc.to = v->destIntersection;
    vc.dir = v->direction;
    vc.color = vehicleColor(v->type);
    vc.state = VState::Approaching;
    vc.stateName = stateToString(vc.state);
    vc.t = 0.f;
    vc.pulseTime = 0.f;

    sf::Vector2f fromPos = (id == IntersectionId::F10) ? F10_POS : F11_POS;
    sf::Vector2f toPos = (vc.to == IntersectionId::F10) ? F10_POS : F11_POS;

    // Determine approach direction based on which intersection vehicle is coming from
    // F10 is on the LEFT (x=280), F11 is on the RIGHT (x=720)
    // If approaching F10 from left: start further left
    // If approaching F11 from left (from F10): start from F10 side
    float approachDir;
    if (id == IntersectionId::F10)
    {
        // Approaching F10: must come from the LEFT (west)
        approachDir = -1.f;
    }
    else
    {
        // Approaching F11: must come from the LEFT (from F10 direction, or west if same intersection)
        approachDir = -1.f;
    }

    vc.startPos = fromPos + sf::Vector2f(approachDir * 200.f, 0.f);
    vc.stopLinePos = fromPos + sf::Vector2f(approachDir * 100.f, 0.f);

    // For straight movement: crossEndPos is past the destination intersection
    if (vc.dir == Direction::Straight)
    {
        // Going straight through means: F10->F11 or F11->F10
        if (id == IntersectionId::F10 && vc.to == IntersectionId::F11)
        {
            // F10 to F11: exit on the RIGHT side of F11
            vc.crossEndPos = toPos + sf::Vector2f(100.f, 0.f);
        }
        else if (id == IntersectionId::F11 && vc.to == IntersectionId::F10)
        {
            // F11 to F10: exit on the LEFT side of F10
            vc.crossEndPos = toPos + sf::Vector2f(-100.f, 0.f);
        }
        else
        {
            // Same intersection (U-turn or error case)
            vc.crossEndPos = fromPos + sf::Vector2f(-approachDir * 100.f, 0.f);
        }
    }
    else
    {
        // For turns, will be computed in computePath
        vc.crossEndPos = fromPos + sf::Vector2f(-approachDir * 100.f, 0.f);
    }
    vc.useBezier = false;

    g_cars.push_back(vc);
    g_stats.totalVehicles++;
    if (v->type == VehicleType::Ambulance || v->type == VehicleType::FireTruck)
    {
        g_stats.emergencyCount++;
    }
}

static void ui_loop()
{
    tryLoadFont();

    g_lights[IntersectionId::F10] = LightColor::RED;
    g_lights[IntersectionId::F11] = LightColor::RED;
    g_preempts[IntersectionId::F10] = false;
    g_preempts[IntersectionId::F11] = false;
    g_stats = Stats();

    sf::RenderWindow window(sf::VideoMode(WINDOW_W, WINDOW_H), "Traffic Simulation - F10 & F11 Intersections");
    window.setFramerateLimit(60);

    sf::Clock clock;
    float time = 0.f;

    while (g_running.load() && window.isOpen())
    {
        sf::Event e;
        while (window.pollEvent(e))
        {
            if (e.type == sf::Event::Closed)
            {
                g_running.store(false);
                window.close();
            }
        }

        if (!g_running.load())
            break;

        float dt = clock.restart().asSeconds();
        time += dt;
        g_timeElapsed += dt;

        // Dark gradient background
        window.clear(sf::Color(15, 18, 25));

        drawHeader(window);
        drawRoads(window);
        drawIntersection(window, IntersectionId::F10, F10_POS, time);
        drawIntersection(window, IntersectionId::F11, F11_POS, time);

        // Parking lots - adjusted positions
        drawParking(window, F10_parking, F10_POS + sf::Vector2f(-150.f, 150.f));
        drawParking(window, F11_parking, F11_POS + sf::Vector2f(15.f, 150.f));

        drawVehicles(window, dt);
        drawInfoPanel(window);
        drawLegend(window);

        // Emergency banner when preemption is active
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            if (g_preempts[IntersectionId::F10] || g_preempts[IntersectionId::F11])
            {
                float flash = (std::sin(time * 8.f) > 0.f) ? 1.f : 0.6f;
                sf::RectangleShape banner(sf::Vector2f(WINDOW_W, 40));
                banner.setPosition(0, 65);
                banner.setFillColor(sf::Color(180, 0, 0, 150 * flash));
                window.draw(banner);

                std::string msg = "⚠️ EMERGENCY VEHICLE - PRIORITY ACTIVE";
                if (g_preempts[IntersectionId::F10] && g_preempts[IntersectionId::F11])
                {
                    msg += " (F10 & F11)";
                }
                else if (g_preempts[IntersectionId::F10])
                {
                    msg += " (F10)";
                }
                else
                {
                    msg += " (F11)";
                }
                drawText(window, msg, sf::Vector2f(WINDOW_W / 2 - 220, 72), 17, sf::Color::White, true);
            }
        }

        updateEvents(dt);

        window.display();
    }

    if (window.isOpen())
    {
        window.close();
    }
}

// Public API (same interface)
void ui_start()
{
    if (g_running.exchange(true))
        return;
    g_ui_thread = std::thread(ui_loop);
}

void ui_stop()
{
    g_running.store(false);
    if (g_ui_thread.joinable())
        g_ui_thread.join();
    if (g_font)
    {
        delete g_font;
        g_font = nullptr;
        g_font_loaded = false;
    }
}

void ui_notify_vehicle_approach(IntersectionId id, Vehicle *v)
{
    addApproachVehicle(id, v);
}

void ui_notify_vehicle_enter(IntersectionId id, Vehicle *v)
{
    (void)id;
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto &c : g_cars)
    {
        if (c.id == v->id)
        {
            c.state = VState::Crossing;
            c.stateName = stateToString(c.state);
            c.t = 0.f;
            computePath(c);
            break;
        }
    }
}

void ui_notify_vehicle_exit(IntersectionId id, Vehicle *v)
{
    (void)id;
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto &c : g_cars)
    {
        if (c.id == v->id)
        {
            c.state = VState::Leaving;
            c.stateName = stateToString(c.state);
            c.t = 0.f;
            g_stats.completed++;
            break;
        }
    }
}

void ui_notify_vehicle_parking(int vehicleId, bool entering)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto &c : g_cars)
    {
        if (c.id == vehicleId)
        {
            if (entering)
            {
                c.state = VState::Parked;
                c.stateName = stateToString(c.state);
                g_stats.parkedCount++;
            }
            else
            {
                c.state = VState::Leaving;
                c.stateName = stateToString(c.state);
                c.t = 0.f;
            }
            break;
        }
    }
}

void ui_update_signal(IntersectionId id, LightColor color)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_lights[id] = color;
}

void ui_notify_emergency_preempt(IntersectionId id, bool active)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_preempts[id] = active;
}

void ui_log_event(const std::string &message)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    EventLog ev;
    ev.message = message.substr(0, 45);
    ev.lifetime = 10.f;
    g_events.push_front(ev);
    if (g_events.size() > 25)
        g_events.pop_back();
}