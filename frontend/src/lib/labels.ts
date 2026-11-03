const NAMES: Record<string, string> = { dp: 'Dynamic programming', unknown: 'Unknown', hashing: 'Hashing' };
/** "brute_force_pair_search" becomes "Brute force pair search". */
export const humanize = (s: string | null | undefined): string =>
  !s ? '-' : NAMES[s] ?? s.replace(/_/g, ' ').replace(/^./, (c) => c.toUpperCase());
