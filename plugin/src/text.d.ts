/**
 * @file Type of Markdown files imported as text (`import x from "a.md" with { type: "text" }`).
 */

declare module "*.md" {
  /** File contents. */
  const text: string;
  export default text;
}
