import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { defineConfigWithTheme } from "vitepress";
import { newsFeed } from "../scripts/news.mjs";
import gameApi from "./game-api.json";
import { flashCodeTheme } from "./theme/code-theme";
import type { DocsThemeConfig } from "./theme/types";

const repo = "https://github.com/Swofty-Developments/SwoftyLauncher";
const base = process.env.DOCS_BASE ?? "/";
const site = "https://f1.swofty.net";

const fonts =
  "https://fonts.googleapis.com/css2?family=Open+Sans:wght@400;600;700&family=Poppins:wght@400;600;700&family=Quicksand:wght@600;700&family=Source+Code+Pro:wght@400;500;600&family=Varela+Round&display=swap";

const pickTheme = `try{var t=localStorage.getItem("f1mm-docs-theme");if(t==="dark"||t==="ocean"||t==="peach")document.documentElement.classList.add("flash-theme-"+t)}catch(e){}`;

const pickSidebar = `try{if(localStorage.getItem("f1mm-docs-sidebar")==="closed")document.documentElement.classList.add("flash-sidebar-closed")}catch(e){}`;

const moved: Record<string, string> = {
  "/tutorials/first-addon": "/reference/addons/first-addon",
  "/tutorials/addon-settings": "/reference/addons/settings",
  "/tutorials/hooking": "/reference/addons/hooking",
  "/tutorials/in-game-ui": "/reference/addons/in-game-ui",
  "/tutorials/database": "/reference/data/database",
  "/tutorials/erp-assets": "/reference/data/textures",
  "/modding/template": "/reference/addons/template",
  "/modding/manifest": "/reference/addons/manifest",
  "/modding/native-plugins": "/reference/addons/native-plugins",
  "/modding/database-ops": "/reference/ops/database",
  "/modding/erp-ops": "/reference/ops/erp",
  "/modding/ui-patches": "/reference/ops/ui-patches",
  "/reference/std/files": "/reference/ops/files",
  "/using/mods-and-maps": "/using/addons",
  "/using/importing-maps": "/using/mods",
  "/guide/addons-mods-maps": "/guide/addons-mods-tracks",
  "/reference/maps": "/reference/installing-tracks",
  "/guide/making-a-mod": "/reference/mods/",
  "/guide/making-an-addon": "/reference/addons/",
  "/reference/mods/database": "/reference/data/database",
  "/reference/mods/text": "/reference/data/text",
  "/reference/mods/erp-assets": "/reference/data/textures",
  "/reference/mods/livery": "/reference/data/livery",
  "/reference/mods/packaging": "/reference/addons/publishing",
  "/reference/mods/publishing": "/reference/addons/publishing",
};

const gameApiItems = gameApi.subsystems.map((s) => ({
  text: s.name,
  link: s.link,
  linkText: `${s.name}.hpp`,
  icon: "file",
  items: s.entries.map((e) => ({ text: e.name, link: e.link, icon: e.kind })),
}));

export default defineConfigWithTheme<DocsThemeConfig>({
  title: "Swofty Launcher for F1 25",
  description: "Addons and mods for F1 25 on Windows and Linux",
  base,
  lang: "en-AU",
  appearance: "force-dark",
  srcExclude: ["**/public/**", "scripts/**"],
  sitemap: { hostname: site },
  buildEnd({ outDir }) {
    writeFileSync(join(outDir, "news.json"), `${JSON.stringify(newsFeed())}\n`);
    for (const [from, to] of Object.entries(moved)) {
      const url = to.endsWith("/") ? `${base}${to.slice(1)}` : `${base}${to.slice(1)}.html`;
      const file = join(outDir, `${from}.html`);
      mkdirSync(dirname(file), { recursive: true });
      writeFileSync(
        file,
        `<!doctype html><meta charset="utf-8"><title>Moved</title><link rel="canonical" href="${url}"><meta http-equiv="refresh" content="0; url=${url}"><script>location.replace(${JSON.stringify(url)} + location.hash)</script><a href="${url}">${url}</a>\n`,
      );
    }
  },
  head: [
    ["link", { rel: "icon", href: `${base}logo.svg`, type: "image/svg+xml" }],
    ["meta", { name: "theme-color", content: "#2a1b39" }],
    ["meta", { property: "og:site_name", content: "Swofty Launcher for F1 25" }],
    ["meta", { property: "og:image", content: `${site}/screenshots/hero-launcher.png` }],
    ["link", { rel: "preconnect", href: "https://fonts.googleapis.com" }],
    ["link", { rel: "preconnect", href: "https://fonts.gstatic.com", crossorigin: "" }],
    ["link", { rel: "stylesheet", href: fonts }],
    ["script", {}, pickTheme],
    ["script", {}, pickSidebar],
  ],
  markdown: {
    theme: flashCodeTheme,
    lineNumbers: true,
    config(md) {
      const { rules } = md.renderer;
      const code = rules.code_inline!;
      rules.table_open = (tokens, idx, options, env, self) => {
        env.inTable = true;
        return `<div class="table-wrap">${self.renderToken(tokens, idx, options)}`;
      };
      rules.table_close = (tokens, idx, options, env, self) => {
        env.inTable = false;
        return `${self.renderToken(tokens, idx, options)}</div>\n`;
      };
      const cell: typeof code = (tokens, idx, options, _env, self) => {
        const inline = tokens[idx + 1];
        if (inline?.type === "inline" && inline.content.length <= 20) tokens[idx].attrJoin("class", "short");
        return self.renderToken(tokens, idx, options);
      };
      rules.th_open = cell;
      rules.td_open = cell;
      rules.code_inline = (tokens, idx, options, env, self) => {
        if (!env.inTable) return code(tokens, idx, options, env, self);
        const t = tokens[idx];
        const body = md.utils.escapeHtml(t.content).replace(/\.(?=[A-Za-z_])/g, ".<wbr>");
        return `<code${self.renderAttrs(t)}>${body}</code>`;
      };
    },
  },
  themeConfig: {
    logo: "/logo.svg",
    siteTitle: "Swofty Launcher for F1 25",
    repo,
    socialLinks: [{ icon: "github", link: repo }],
    search: {
      provider: "local",
    },
    tabs: [
      {
        id: "users",
        text: "Users",
        icon: "book",
        groups: [
          {
            text: "Getting started",
            items: [
              { text: "Introduction", link: "/guide/" },
              { text: "Addons, mods and tracks", link: "/guide/addons-mods-tracks" },
              { text: "Installation", link: "/guide/installation" },
              { text: "Windows", link: "/guide/windows" },
              { text: "Linux and Steam Deck", link: "/guide/linux" },
            ],
          },
          {
            text: "Using the launcher",
            items: [
              { text: "Overview", link: "/using/" },
              { text: "News", link: "/using/news" },
              { text: "Addons", link: "/using/addons" },
              { text: "Addon Browser", link: "/using/addon-browser" },
              { text: "Swofty's addons", link: "/using/swofty-addons" },
              { text: "Simplified Importer", link: "/using/mods" },
              { text: "Profiles", link: "/using/profiles" },
              { text: "In-game addon settings", link: "/using/swofty-mod" },
              { text: "Steam and EA app copies", link: "/using/game-copies" },
              { text: "Playing in VR", link: "/using/vr" },
              { text: "Career Mode Editor", link: "/using/career-editor" },
              { text: "Driver Manager", link: "/using/driver-manager" },
              { text: "DB Editor", link: "/using/db-editor" },
              { text: "Tools", link: "/using/tools" },
              { text: "Restoring the game", link: "/using/profiles-and-restoring" },
              { text: "Updating", link: "/using/updating" },
              { text: "Sending your logs", link: "/using/send-logs" },
              { text: "Uninstalling", link: "/guide/uninstall" },
            ],
          },
          {
            text: "How it works",
            items: [
              { text: "Launching the game", link: "/reference/launch" },
              { text: "Offline play", link: "/reference/safety" },
              { text: "Backups and recovery", link: "/reference/restore" },
            ],
          },
          {
            text: "Releases",
            items: [
              { text: "1.5.37", link: "/releases/1.5.37" },
              { text: "1.5.36", link: "/releases/1.5.36" },
              { text: "1.5.35", link: "/releases/1.5.35" },
              { text: "1.5.34", link: "/releases/1.5.34" },
              { text: "1.5.33", link: "/releases/1.5.33" },
              { text: "1.5.32", link: "/releases/1.5.32" },
              { text: "1.5.31", link: "/releases/1.5.31" },
              { text: "1.5.30", link: "/releases/1.5.30" },
              { text: "1.5.29", link: "/releases/1.5.29" },
              { text: "1.5.28", link: "/releases/1.5.28" },
              { text: "1.5.27", link: "/releases/1.5.27" },
              { text: "1.5.26", link: "/releases/1.5.26" },
              { text: "1.5.25", link: "/releases/1.5.25" },
              { text: "1.5.24", link: "/releases/1.5.24" },
              { text: "1.5.23", link: "/releases/1.5.23" },
              { text: "1.5.22", link: "/releases/1.5.22" },
              { text: "1.5.21", link: "/releases/1.5.21" },
              { text: "1.5.20", link: "/releases/1.5.20" },
              { text: "1.5.19", link: "/releases/1.5.19" },
              { text: "1.5.18", link: "/releases/1.5.18" },
              { text: "1.5.17", link: "/releases/1.5.17" },
              { text: "1.5.16", link: "/releases/1.5.16" },
              { text: "1.5.15", link: "/releases/1.5.15" },
              { text: "1.5.14", link: "/releases/1.5.14" },
              { text: "1.5.13", link: "/releases/1.5.13" },
              { text: "1.5.12", link: "/releases/1.5.12" },
              { text: "1.5.11", link: "/releases/1.5.11" },
              { text: "1.5.10", link: "/releases/1.5.10" },
              { text: "1.5.9", link: "/releases/1.5.9" },
              { text: "1.5.8", link: "/releases/1.5.8" },
              { text: "1.5.7", link: "/releases/1.5.7" },
              { text: "1.5.6", link: "/releases/1.5.6" },
              { text: "1.5.5", link: "/releases/1.5.5" },
              { text: "1.5.4", link: "/releases/1.5.4" },
              { text: "1.5.3", link: "/releases/1.5.3" },
              { text: "1.5.2", link: "/releases/1.5.2" },
              { text: "1.5.1", link: "/releases/1.5.1" },
              { text: "1.5.0", link: "/releases/1.5.0" },
              { text: "1.4.0", link: "/releases/1.4.0" },
              { text: "1.3.7", link: "/releases/1.3.7" },
              { text: "1.3.6", link: "/releases/1.3.6" },
              { text: "1.3.5", link: "/releases/1.3.5" },
              { text: "1.3.4", link: "/releases/1.3.4" },
              { text: "1.3.3", link: "/releases/1.3.3" },
              { text: "1.3.2", link: "/releases/1.3.2" },
              { text: "1.3.1", link: "/releases/1.3.1" },
              { text: "1.3.0", link: "/releases/1.3.0" },
              { text: "1.2.6", link: "/releases/1.2.6" },
              { text: "1.2.5", link: "/releases/1.2.5" },
              { text: "1.2.4", link: "/releases/1.2.4" },
              { text: "1.2.3", link: "/releases/1.2.3" },
              { text: "1.2.2", link: "/releases/1.2.2" },
              { text: "1.2.1", link: "/releases/1.2.1" },
              { text: "1.2.0", link: "/releases/1.2.0" },
              { text: "1.1.0", link: "/releases/1.1.0" },
              { text: "1.0.13", link: "/releases/1.0.13" },
              { text: "1.0.12", link: "/releases/1.0.12" },
              { text: "1.0.11", link: "/releases/1.0.11" },
              { text: "1.0.10", link: "/releases/1.0.10" },
              { text: "1.0.9", link: "/releases/1.0.9" },
              { text: "1.0.8", link: "/releases/1.0.8" },
              { text: "1.0.7", link: "/releases/1.0.7" },
              { text: "1.0.6", link: "/releases/1.0.6" },
              { text: "1.0.5", link: "/releases/1.0.5" },
              { text: "1.0.4", link: "/releases/1.0.4" },
              { text: "1.0.3", link: "/releases/1.0.3" },
              { text: "1.0.2", link: "/releases/1.0.2" },
              { text: "1.0.1", link: "/releases/1.0.1" },
              { text: "1.0.0", link: "/releases/1.0.0" },
              { text: "0.2.10", link: "/releases/0.2.10" },
              { text: "0.2.9", link: "/releases/0.2.9" },
              { text: "0.2.8", link: "/releases/0.2.8" },
              { text: "0.2.7", link: "/releases/0.2.7" },
              { text: "0.2.6", link: "/releases/0.2.6" },
              { text: "0.2.5", link: "/releases/0.2.5" },
              { text: "0.2.4", link: "/releases/0.2.4" },
              { text: "0.2.3", link: "/releases/0.2.3" },
              { text: "0.2.2", link: "/releases/0.2.2" },
              { text: "0.2.1", link: "/releases/0.2.1" },
              { text: "0.2.0", link: "/releases/0.2.0" },
            ],
          },
        ],
      },
      {
        id: "developers",
        text: "Developers",
        icon: "code",
        mono: true,
        groups: [
          {
            text: "Start here",
            items: [{ text: "Developer overview", link: "/reference/", icon: "file" }],
          },
          {
            text: "Mods",
            items: [
              { text: "Overview", link: "/reference/mods/", icon: "file" },
              { text: "Track packages", link: "/reference/mods/tracks", icon: "file" },
              { text: ".serps packages", link: "/reference/mods/serps", icon: "file" },
            ],
          },
          {
            text: "Addons",
            items: [
              { text: "Overview", link: "/reference/addons/", icon: "file" },
              { text: "Your first addon", link: "/reference/addons/first-addon", icon: "file" },
              { text: "Templates", link: "/reference/addons/template", icon: "file" },
              { text: "Settings", link: "/reference/addons/settings", icon: "file" },
              { text: "Shader packs", link: "/reference/addons/shader-packs", icon: "file" },
              { text: "Post-process passes", link: "/reference/addons/postfx", icon: "file" },
              { text: "Packaging and publishing", link: "/reference/addons/publishing", icon: "file" },
            ],
          },
          {
            text: "Data addons",
            items: [
              { text: "Overview", link: "/reference/data/", icon: "file" },
              { text: "Operations", link: "/reference/data/operations", icon: "file" },
              { text: "Game database", link: "/reference/data/database", icon: "file" },
              { text: "In-game text", link: "/reference/data/text", icon: "file" },
              { text: "Textures and models", link: "/reference/data/textures", icon: "file" },
              { text: "Liveries", link: "/reference/data/livery", icon: "file" },
              { text: "A track with its own name", link: "/reference/data/track-names", icon: "file" },
            ],
          },
          {
            text: "Menus and UI",
            items: [
              { text: "Overview", link: "/reference/ui/", icon: "file" },
              { text: "UI scenes (.idf)", link: "/reference/ui/idf", icon: "file" },
              { text: "Swofty UI", link: "/reference/ui/swofty-ui", icon: "file" },
              { text: "Editing menus", link: "/reference/ui/editing-menus", icon: "file" },
              { text: "Techniques", link: "/reference/ui/techniques", icon: "file" },
              { text: "Debugging live", link: "/reference/ui/debugging", icon: "file" },
              { text: "Node reference", link: "/reference/ui/nodes", icon: "file" },
              { text: "Patterns and checks", link: "/reference/ui/patterns", icon: "file" },
              { text: "UI engine internals", link: "/reference/ui/engine", icon: "file" },
            ],
          },
          {
            text: "Engine plugins",
            items: [
              { text: "Overview", link: "/reference/addons/native-plugins", icon: "file" },
              { text: "The mixin system", link: "/reference/addons/hooking", icon: "file" },
              { text: "In-game UI", link: "/reference/addons/in-game-ui", icon: "file" },
              {
                text: "Standard library",
                link: "/reference/std/",
                icon: "file",
                items: [
                  { text: "Events & frame", link: "/reference/std/events", icon: "file" },
                  { text: "Overlay", link: "/reference/std/overlay", icon: "file" },
                  { text: "Telemetry", link: "/reference/std/telemetry", icon: "file" },
                  { text: "Database", link: "/reference/std/db", icon: "file" },
                  { text: "Storage", link: "/reference/std/storage", icon: "file" },
                  { text: "Career", link: "/reference/std/career", icon: "file" },
                  { text: "Popup", link: "/reference/std/popup", icon: "file" },
                  { text: "Race HUD", link: "/reference/std/hud", icon: "file" },
                  { text: "Strings", link: "/reference/std/strings", icon: "file" },
                  { text: "Career contents", link: "/reference/std/career-content", icon: "file" },
                  { text: "Characters", link: "/reference/std/characters", icon: "file" },
                  { text: "Career wizard", link: "/reference/std/career-wizard", icon: "file" },
                  { text: "Era picker boxes", link: "/reference/std/era-picker", icon: "file" },
                  { text: "Memory", link: "/reference/std/memory", icon: "file" },
                  { text: "Logging", link: "/reference/std/logging", icon: "file" },
                  { text: "Mixin API", link: "/reference/std/mixin", icon: "file" },
                  { text: "Settings", link: "/reference/std/settings", icon: "file" },
                  { text: "UI", link: "/reference/std/ui", icon: "file" },
                ],
              },
            ],
          },
          {
            text: "Launcher plugins",
            items: [
              { text: "Overview and protocol", link: "/reference/addons/launcher-api", icon: "file" },
              { text: "Changes at runtime", link: "/reference/addons/runtime-assets", icon: "file" },
            ],
          },
          {
            text: "Engine API (headers)",
            items: [{ text: "Overview", link: "/reference/game/", icon: "file" }, ...gameApiItems],
          },
          {
            text: "Reference",
            items: [
              { text: "Addon manifest", link: "/reference/addons/manifest", icon: "file" },
              {
                text: "Operations",
                link: "/reference/data/operations",
                icon: "file",
                items: [
                  { text: "Database operations", link: "/reference/ops/database", icon: "file" },
                  { text: "ERP operations", link: "/reference/ops/erp", icon: "file" },
                  { text: "Asset op files", link: "/reference/addons/asset-api", icon: "file" },
                  { text: "File operations", link: "/reference/ops/files", icon: "file" },
                  { text: "UI patches", link: "/reference/ops/ui-patches", icon: "file" },
                ],
              },
              { text: "Command line (swofty)", link: "/reference/cli", icon: "file" },
              {
                text: "Game database (GVDB)",
                link: "/reference/gamedb",
                icon: "file",
                items: [
                  { text: "File format", link: "/reference/gamedb/format", icon: "file" },
                  { text: "How the game reads it", link: "/reference/gamedb/reading", icon: "file" },
                  { text: "Modding recipes", link: "/reference/gamedb/recipes", icon: "file" },
                  { text: "Tools", link: "/reference/gamedb/tools", icon: "file" },
                  { text: "Table catalogue", link: "/reference/gamedb/catalogue", icon: "file" },
                  { text: "Key schema", link: "/reference/gamedb/schema", icon: "file" },
                ],
              },
              { text: "ERP archives", link: "/reference/erp", icon: "file" },
              { text: "!pkg resources", link: "/reference/pkg", icon: "file" },
              { text: "Assets from older F1 games", link: "/reference/old-games", icon: "file" },
              { text: "How tracks are installed", link: "/reference/installing-tracks", icon: "file" },
            ],
          },
          {
            text: "Contributing",
            items: [
              { text: "Development", link: "/reference/development", icon: "file" },
              { text: "Design overview", link: "/design/", icon: "file" },
              { text: "Addons and mods design", link: "/design/addons-and-mods", icon: "file" },
              { text: "Native libraries", link: "/design/native-libraries", icon: "file" },
            ],
          },
        ],
      },
    ],
  },
});
