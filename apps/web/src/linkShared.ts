/** A toolkit bundle's source with its bare imports of react pointed at `urls` (the app's copies, pb#353). */
export function linkShared(source: string, urls: Record<string, string>): string {
  return source.replace(
    /(\bfrom\s*|\bimport\s*\(\s*|\bimport\s*)(["'])(react|react-dom|react\/jsx-runtime)\2/g,
    (_, lead: string, q: string, spec: string) => `${lead}${q}${urls[spec]}${q}`,
  );
}
