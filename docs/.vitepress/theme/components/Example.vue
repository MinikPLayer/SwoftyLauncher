<script setup lang="ts">
import { computed, ref, useSlots } from "vue";

const props = defineProps<{
  root?: string;
  codeFile?: string;
  dataFile?: string;
  files?: string;
  title?: string;
}>();

const slots = useSlots();
const hasCode = computed(() => !!slots.code);
const hasData = computed(() => !!slots.data);
const tab = ref<"code" | "data">(hasCode.value ? "code" : "data");
const showTree = ref(false);

const current = computed(() => (tab.value === "code" ? props.codeFile : props.dataFile) ?? "");
const crumbs = computed(() => {
  const parts = current.value.split("/").filter(Boolean);
  return props.root ? [props.root, ...parts] : parts;
});

type Row = { depth: number; name: string; dir: boolean; path: string };
const rows = computed<Row[]>(() => {
  const list = (props.files ?? "")
    .split(",")
    .map((f) => f.trim())
    .filter(Boolean);
  for (const f of [props.codeFile, props.dataFile]) if (f && !list.includes(f)) list.push(f);
  const seen = new Set<string>();
  const out: Row[] = [];
  for (const f of [...list].sort()) {
    const parts = f.split("/");
    parts.forEach((name, i) => {
      const path = parts.slice(0, i + 1).join("/");
      if (seen.has(path)) return;
      seen.add(path);
      out.push({ depth: i, name, dir: i < parts.length - 1, path });
    });
  }
  return out;
});
</script>

<template>
  <div class="sx-example">
    <div class="sx-head">
      <div class="sx-tabs" role="tablist">
        <button
          v-if="hasCode"
          type="button"
          role="tab"
          :aria-selected="tab === 'code'"
          :class="{ on: tab === 'code' }"
          @click="tab = 'code'"
        >
          C++
        </button>
        <button
          v-if="hasData"
          type="button"
          role="tab"
          :aria-selected="tab === 'data'"
          :class="{ on: tab === 'data' }"
          @click="tab = 'data'"
        >
          Without code
        </button>
        <span v-if="title" class="sx-title">{{ title }}</span>
      </div>
      <button
        v-if="rows.length"
        type="button"
        class="sx-files"
        :class="{ on: showTree }"
        :aria-expanded="showTree"
        @click="showTree = !showTree"
      >
        Files
      </button>
    </div>
    <div v-if="crumbs.length" class="sx-path" :title="current">
      <template v-for="(c, i) in crumbs" :key="i">
        <span v-if="i" class="sep">/</span><span :class="{ leaf: i === crumbs.length - 1 }">{{ c }}</span>
      </template>
    </div>
    <div v-if="showTree" class="sx-tree">
      <div v-if="root" class="row dir">{{ root }}/</div>
      <div
        v-for="r in rows"
        :key="r.path"
        class="row"
        :class="{ dir: r.dir, here: r.path === current }"
        :style="{ paddingLeft: `${(r.depth + (root ? 1 : 0)) * 1.1 + 0.6}rem` }"
      >
        {{ r.name }}{{ r.dir ? "/" : "" }}
      </div>
    </div>
    <div v-show="tab === 'code'" class="sx-body"><slot name="code" /></div>
    <div v-show="tab === 'data'" class="sx-body"><slot name="data" /></div>
  </div>
</template>
