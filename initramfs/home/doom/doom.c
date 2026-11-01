/*
 * ProotOS — DOOM-like ASCII raycasting shooter (single-file demo).
 *
 * Controls: W/S forward/back, A/D strafe, arrows turn/move, Space fire,
 *           R restart map, Q quit.
 */
#include "libc.h"
#include <stdio.h>
#include <math.h>

#define MAXW 80
#define MAXH 24
#define VIEW_H 20
#define MAX_ENEMIES 16

struct enemy {
    double x, y;
    int hp;
    int alive;
};

static const char *map_rows[] = {
    "########################################",
    "#P             #              #        #",
    "#      ####    #    E         #   E    #",
    "#              #              #        #",
    "#   E          #      ####    #        #",
    "#              #              #   X    #",
    "#        ######              ##########",
    "#              #      E               #",
    "######    ######              ####    #",
    "#              #                      #",
    "#   E          ########################",
    "#                                     #",
    "#          E                          #",
    "########################################",
    "########################################",
    "#P        #            #              #",
    "#   E     #     E      #      E       #",
    "#         #            #              #",
    "#####     #     ########              #",
    "#         #                           #",
    "#    E    #      X                    #",
    "#         ##########      ####        #",
    "#                                     #",
    "########################################",
    "########################################",
    "#P    #          #          #         #",
    "#  E  #    E     #    E     #    E    #",
    "#     #          #          #         #",
    "#     ######     #     ######         #",
    "#          #     #          #         #",
    "#    E     #     #     E    #     X   #",
    "#          #     #          #         #",
    "#          ##################         #",
    "#                                     #",
    "########################################",
};

static const int map_start[3] = {0, 14, 24};
static const int map_end[3] = {14, 24, 34};

static int map_w, map_h;
static char grid[MAXH][MAXW + 1];
static double px, py, dirx, diry, planex, planey;
static struct enemy enemies[MAX_ENEMIES];
static int enemy_count, alive_count;
static int hp, ammo, map_index, won;
static int term_w = 78, term_h = 24;
static char screen[VIEW_H][MAXW + 1];
static double wall_dist[MAXW];

static void load_map(int idx) {
    map_h = 0;
    map_w = 0;
    int y = 0;
    for (int line = map_start[idx]; line < map_end[idx] && y < MAXH; line++) {
        const char *src = map_rows[line];
        int len = (int)strlen(src);
        if (len > MAXW) len = MAXW;
        if (len > map_w) map_w = len;
        for (int x = 0; x < len; x++)
            grid[y][x] = src[x];
        grid[y][len] = 0;
        y++;
    }
    while (y < MAXH) {
        for (int i = 0; i <= MAXW; i++) grid[y][i] = '#';
        grid[y][MAXW] = 0;
        y++;
    }
    map_h = y;
    enemy_count = 0;
    for (int r = 0; r < map_h; r++) {
        for (int c = 0; c < map_w; c++) {
            char ch = grid[r][c];
            if (ch == 'P') {
                px = c + 0.5;
                py = r + 0.5;
                dirx = 1.0; diry = 0.0;
                planex = 0.0; planey = 0.66;
            } else if (ch == 'E' && enemy_count < MAX_ENEMIES) {
                enemies[enemy_count].x = c + 0.5;
                enemies[enemy_count].y = r + 0.5;
                enemies[enemy_count].hp = 1;
                enemies[enemy_count].alive = 1;
                enemy_count++;
            }
        }
    }
    alive_count = enemy_count;
    hp = 100;
    ammo = 50;
}

static int is_wall(double x, double y) {
    int cx = (int)x, cy = (int)y;
    if (cx < 0 || cy < 0 || cx >= map_w || cy >= map_h) return 1;
    char c = grid[cy][cx];
    if (c == '#' || c == '=') return 1;
    if (c == 'X' && alive_count > 0) return 0; /* exit is walkable when open */
    return 0;
}

static void render(void) {
    for (int y = 0; y < VIEW_H; y++)
        for (int x = 0; x < term_w; x++)
            screen[y][x] = ' ';
    for (int x = 0; x < term_w; x++) {
        double camera = 2.0 * x / term_w - 1.0;
        double rx = dirx + planex * camera;
        double ry = diry + planey * camera;
        int mx = (int)px, my = (int)py;
        double ddx = rx == 0 ? 1e30 : fabs(1.0 / rx);
        double ddy = ry == 0 ? 1e30 : fabs(1.0 / ry);
        int stepx = rx < 0 ? -1 : 1;
        int stepy = ry < 0 ? -1 : 1;
        double sdx = rx < 0 ? (px - mx) * ddx : (mx + 1.0 - px) * ddx;
        double sdy = ry < 0 ? (py - my) * ddy : (my + 1.0 - py) * ddy;
        int side = 0;
        for (int guard = 0; guard < 512; guard++) {
            if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
            else { sdy += ddy; my += stepy; side = 1; }
            if (mx < 0 || my < 0 || mx >= map_w || my >= map_h) break;
            char c = grid[my][mx];
            if (c == '#' || c == '=') break;
        }
        double dist = side == 0 ? (sdx - ddx) : (sdy - ddy);
        if (dist < 0.05) dist = 0.05;
        wall_dist[x] = dist;
        int lh = (int)(VIEW_H / dist);
        int start = VIEW_H / 2 - lh / 2;
        int end = start + lh;
        if (start < 0) start = 0;
        if (end > VIEW_H) end = VIEW_H;
        static const char *grad0 = "@#%*+=-:.";
        static const char *grad1 = "|!I%+=-:.";
        int lvl = (int)(dist * 1.5);
        if (lvl > 8) lvl = 8;
        char wall = (side ? grad1 : grad0)[lvl];
        for (int y = start; y < end; y++)
            screen[y][x] = wall;
    }
    for (int i = 0; i < enemy_count; i++) {
        if (!enemies[i].alive) continue;
        double dx = enemies[i].x - px;
        double dy = enemies[i].y - py;
        double inv = 1.0 / (planex * diry - dirx * planey);
        double tx = inv * (diry * dx - dirx * dy);
        double ty = inv * (-planey * dx + planex * dy);
        if (ty <= 0.2) continue;
        int sx = (int)(term_w / 2 * (1 + tx / ty));
        if (sx < 0 || sx >= term_w) continue;
        if (ty < wall_dist[sx]) {
            int sy = VIEW_H / 2;
            screen[sy][sx] = 'E';
            if (sx > 0) screen[sy][sx - 1] = '[';
            if (sx + 1 < term_w) screen[sy][sx + 1] = ']';
        }
    }
    /* minimap, bottom-right */
    int mw = 18, mh = 8;
    int ox = term_w - mw;
    int oy = VIEW_H - mh;
    if (ox > 0 && oy > 0) {
        for (int yy = 0; yy < mh; yy++) {
            for (int xx = 0; xx < mw; xx++) {
                int mx = (int)px - mw / 2 + xx;
                int my = (int)py - mh / 2 + yy;
                char c = '.';
                if (mx >= 0 && my >= 0 && mx < map_w && my < map_h) {
                    c = grid[my][mx];
                    if (c == '#' || c == '=') c = '#';
                    else if (c == 'X') c = 'X';
                    else c = '.';
                } else {
                    c = ' ';
                }
                if (mx == (int)px && my == (int)py) c = '@';
                for (int e = 0; e < enemy_count; e++) {
                    if (enemies[e].alive && (int)enemies[e].x == mx &&
                        (int)enemies[e].y == my)
                        c = 'e';
                }
                screen[oy + yy][ox + xx] = c;
            }
        }
    }
    /* weapon sprite */
    static const char *gun[] = {
        "   __   ",
        "  /  \\  ",
        " | () | ",
        " |____| ",
        "  /  \\  ",
    };
    int gx = term_w / 2 - 4;
    int gy = VIEW_H - 5;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; gun[r][c] && c < 8; c++)
            screen[gy + r][gx + c] = gun[r][c];
    }
    if (VIEW_H > 3)
        screen[VIEW_H / 2][term_w / 2] = '+';
}

static void draw(void) {
    for (int y = 0; y < VIEW_H; y++) {
        printf("\x1b[%d;1H", y + 1);
        screen[y][term_w] = 0;
        printf("%s", screen[y]);
    }
    printf("\x1b[%d;1H", VIEW_H + 1);
    printf("HP:%3d AMMO:%2d MAP:%d/%d ENEMIES:%2d  PISTOL   ", hp, ammo, map_index + 1, 3,
           alive_count);
    printf("\x1b[%d;1H", VIEW_H + 2);
    printf("W/S move  A/D strafe  arrows turn  SPACE fire  R restart  Q quit");
    if (alive_count == 0)
        printf("\x1b[%d;1H*** EXIT OPEN — reach X ***                    ", VIEW_H + 1);
    if (won)
        printf("\x1b[%d;1H*** YOU WIN — Q to quit ***                     ", VIEW_H + 1);
}

static void shoot(void) {
    if (ammo <= 0) return;
    ammo--;
    double best = 1e9;
    int best_i = -1;
    for (int i = 0; i < enemy_count; i++) {
        if (!enemies[i].alive) continue;
        double dx = enemies[i].x - px;
        double dy = enemies[i].y - py;
        double dist = sqrt(dx * dx + dy * dy);
        if (dist > 14) continue;
        double dot = (dx * dirx + dy * diry) / (dist > 0 ? dist : 1);
        if (dot < 0.94) continue;
        if (dist < best) { best = dist; best_i = i; }
    }
    if (best_i >= 0) {
        enemies[best_i].hp--;
        if (enemies[best_i].hp <= 0) {
            enemies[best_i].alive = 0;
            alive_count--;
        }
    }
}

static void update_enemies(void) {
    for (int i = 0; i < enemy_count; i++) {
        if (!enemies[i].alive) continue;
        double dx = px - enemies[i].x;
        double dy = py - enemies[i].y;
        double dist = sqrt(dx * dx + dy * dy);
        if (dist < 1.2) {
            hp -= 3;
            if (hp < 0) hp = 0;
            continue;
        }
        if (dist > 14) continue;
        double stepx = dx / dist * 0.09;
        double stepy = dy / dist * 0.09;
        if (!is_wall(enemies[i].x + stepx, enemies[i].y))
            enemies[i].x += stepx;
        if (!is_wall(enemies[i].x, enemies[i].y + stepy))
            enemies[i].y += stepy;
    }
}

static int input_fd = 0;

static int getkey(void) {
    char c;
    long n = read(input_fd, &c, 1);
    if (n == 1) return (unsigned char)c;
    return -1;
}

int main(void) {
    console_info(&term_w, &term_h);
    if (term_w < 40 || term_w > MAXW) term_w = MAXW;
    (void)term_h;
    term_w = 78;
    if (fcntl(0, F_GETFL, 0) < 0) {
        input_fd = open("/dev/kbd", 0, 0);
    } else {
        fcntl(0, F_SETFL, O_NONBLOCK);
    }
    if (input_fd < 0) {
        printf("doom: no input\n");
        return 1;
    }
    printf("\x1b[2J");
    map_index = 0;
    won = 0;
    load_map(0);
    int frame = 0;
    int quit = 0;
    while (!quit) {
        int k;
        while ((k = getkey()) >= 0) {
            if (k == 'q' || k == 'Q' || k == 27) { quit = 1; break; }
            if (k == 'r' || k == 'R') { load_map(map_index); won = 0; }
            if (k == 'w' || k == 'W' || k == 0x80) {
                double nx = px + dirx * 0.15, ny = py + diry * 0.15;
                if (!is_wall(nx, py)) px = nx;
                if (!is_wall(px, ny)) py = ny;
            }
            if (k == 's' || k == 'S' || k == 0x81) {
                double nx = px - dirx * 0.15, ny = py - diry * 0.15;
                if (!is_wall(nx, py)) px = nx;
                if (!is_wall(px, ny)) py = ny;
            }
            if (k == 'a' || k == 'A') {
                double nx = px + diry * 0.12, ny = py - dirx * 0.12;
                if (!is_wall(nx, py)) px = nx;
                if (!is_wall(px, ny)) py = ny;
            }
            if (k == 'd' || k == 'D') {
                double nx = px - diry * 0.12, ny = py + dirx * 0.12;
                if (!is_wall(nx, py)) px = nx;
                if (!is_wall(px, ny)) py = ny;
            }
            if (k == 0x84 || k == '[') {
                double ox = dirx;
                dirx = dirx * 0.94 + diry * 0.34;
                diry = -ox * 0.34 + diry * 0.94;
                planex = -diry * 0.66;
                planey = dirx * 0.66;
            }
            if (k == 0x85 || k == ']') {
                double ox = dirx;
                dirx = dirx * 0.94 - diry * 0.34;
                diry = ox * 0.34 + diry * 0.94;
                planex = -diry * 0.66;
                planey = dirx * 0.66;
            }
            if (k == ' ') shoot();
        }
        if (quit) break;
        if (frame % 5 == 0) update_enemies();
        if (hp <= 0) {
            render();
            draw();
            printf("\x1b[%d;1H*** YOU DIED — R restart, Q quit ***          ", VIEW_H + 1);
            sleep_ms(40);
            frame++;
            continue;
        }
        if (alive_count == 0) {
            int cx = (int)px, cy = (int)py;
            if (cx >= 0 && cy >= 0 && cx < map_w && cy < map_h && grid[cy][cx] == 'X') {
                map_index++;
                if (map_index >= 3) {
                    won = 1;
                    map_index = 2;
                } else {
                    load_map(map_index);
                }
            }
        }
        render();
        draw();
        frame++;
        sleep_ms(40);
    }
    printf("\x1b[2J\x1b[1;1H");
    if (input_fd > 2) close(input_fd);
    return 0;
}
