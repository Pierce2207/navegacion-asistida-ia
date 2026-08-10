const apiUrl = process.env.EXPO_PUBLIC_API_URL;

export function getApiBaseUrl(): string {
  if (!apiUrl) {
    throw new Error('EXPO_PUBLIC_API_URL must be defined in environment variables');
  }
  return apiUrl;
}
