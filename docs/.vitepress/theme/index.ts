import type { Theme } from "vitepress";
import DefaultTheme from "vitepress/theme-without-fonts";
import Layout from "./Layout.vue";
import Landing from "./components/Landing.vue";
import Example from "./components/Example.vue";
import Split from "./components/Split.vue";
import "./styles/flash.css";
import "./styles/docs.css";
import "./styles/landing.css";
import "./styles/examples.css";

export default {
  extends: DefaultTheme,
  Layout,
  enhanceApp({ app }) {
    app.component("Landing", Landing);
    app.component("Example", Example);
    app.component("Split", Split);
  },
} satisfies Theme;
