import assert from "node:assert/strict";
import { test } from "node:test";
import { linkShared } from "../src/linkShared.ts";

const urls = { react: "blob:r", "react-dom": "blob:d", "react/jsx-runtime": "blob:j" };

test("a toolkit bundle's react imports point at the app's copies, and nothing else moves", () => {
  const src = `import{jsx as e}from"react/jsx-runtime";import * as R from 'react';const x=import("react-dom");import"./a.css";import y from"react-icons";`;
  assert.equal(
    linkShared(src, urls),
    `import{jsx as e}from"blob:j";import * as R from 'blob:r';const x=import("blob:d");import"./a.css";import y from"react-icons";`,
  );
});
