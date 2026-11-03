import { ApiError } from '../api/ApiError';

/** Turns any thrown value into one readable sentence for the UI. */
export function errorMessage(e: unknown): string {
  if (e instanceof ApiError) {
    switch (e.code) {
      case 'RATE_LIMITED': return 'Job limit reached (30 per hour). Try again later.';
      case 'CODE_TOO_LARGE': return 'The source is larger than 64 KB.';
      case 'MODEL_UNAVAILABLE': return 'No model is active on the server.';
      case 'USERNAME_TAKEN': return 'That username is already taken.';
      case 'FORBIDDEN': return 'This job belongs to another account.';
      case 'VALIDATION_FAILED': return e.field ? `${e.field}: ${e.message}` : e.message;
      default: return e.message;
    }
  }
  if (e instanceof TypeError) return 'Cannot reach the server. Check that the backend is running on port 8080.';
  return (e as Error)?.message || 'Unexpected error.';
}

/** Job-level error codes from the spec (section 5, "When failed"). */
export const JOB_ERROR_TITLE: Record<string, string> = {
  COMPILE_ERROR: 'The code did not compile',
  PARSE_ERROR: 'The code could not be parsed',
  TIMEOUT: 'The run exceeded its time limit',
  SANDBOX_ERROR: 'The sandbox could not run the code',
  MODEL_UNAVAILABLE: 'No model is loaded',
  INTERNAL: 'Internal server error',
};
