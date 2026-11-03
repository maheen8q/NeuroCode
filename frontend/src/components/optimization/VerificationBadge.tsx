import { ShieldAlert, ShieldCheck } from 'lucide-react';
import { Badge } from '../common/ui';

export default function VerificationBadge({ verified, testsRun, mismatches }: { verified: boolean; testsRun?: number; mismatches?: number }) {
  return verified
    ? <Badge tone="ok" className="!px-2.5 !py-1 !text-sm"><ShieldCheck className="h-4 w-4" />Verified by testing{testsRun !== undefined ? `: ${testsRun} tests, ${mismatches ?? 0} mismatches` : ''}</Badge>
    : <Badge tone="warn" className="!px-2.5 !py-1 !text-sm"><ShieldAlert className="h-4 w-4" />Not verified</Badge>;
}
